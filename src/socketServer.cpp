#include "socket.hpp"

SocketServer::SocketServer(uint16_t port){
    // Create socket
    serverFD = ::socket(AF_INET, SOCK_STREAM, 0);
    if(serverFD == -1){
        throw std::system_error(errno, std::generic_category(), "socket() failed");
    }
    else{
        std::cout << std::endl << "Success: Created socket " << serverFD;
    }
    
    // Server configurations
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // Set socket options
    const int reuse = 1;
    if(::setsockopt(serverFD, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))){
        ::close(serverFD);
        throw std::system_error(errno, std::generic_category(), "setsockopt() failed");
    }
    else{
        std::cout << std::endl << "Success: Set socket options";
    }

    // Set socket to blocking mode
    int flags = ::fcntl(serverFD, F_GETFL, 0);
    if(flags == -1){
        ::close(serverFD);
        throw std::system_error(errno, std::generic_category(), "fcntl(F_GETFL) failed");
    }

    if(::fcntl(serverFD, F_SETFL, flags & ~O_NONBLOCK) == -1){
        ::close(serverFD);
        throw std::system_error(errno, std::generic_category(), "fcntl(F_SETFL) failed");
    }
    else{
        std::cout << std::endl << "Success: Set to blocking mode";
    }
    
    // Bind
    if(::bind(serverFD, reinterpret_cast<const sockaddr*>(&serverAddr), sizeof(serverAddr)) == -1){
        ::close(serverFD);
        throw std::system_error(errno, std::generic_category(), "bind() failed");
    }
    else{
        std::cout << std::endl << "Success: Bind";
    }

    // Listen
    if(::listen(serverFD, 5)){
        ::close(serverFD);
        throw std::system_error(errno, std::generic_category(), "listen() failed");
    }
    else{
        std::cout << std::endl << "Success: Listen ";
    }
    std::cout << std::endl << std::endl << "Server listening on port " << port << "..." << std::endl << std::endl;
}

SocketServer::~SocketServer(){
    if(serverFD != -1){
        close(serverFD);
        std::cout << std::endl << "Server socket " << serverFD << " closed" << std::endl;
    }
}

constexpr auto SocketServer::generateCRCTable() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
        table[i] = crc;
    }
    return table;
}

uint32_t SocketServer::crc32(const std::string& str) {
    static constexpr auto crc_table = generateCRCTable();
    uint32_t crc = 0xFFFFFFFF;
    for (char c : str) {
        crc = (crc >> 8) ^ crc_table[(crc ^ static_cast<uint8_t>(c)) & 0xFF];
    }
    return ~crc;
}

void SocketServer::handleClientDisconnect(int clientFD){
    if (clientFD == -1) return;
    std::cout << std::endl << "Client " << clientFD << " disconnected" << std::endl << std::flush;
    close(clientFD);
}

void SocketServer::handleLoginResponse(int clientFD, bool validStatus, const MessageHeader &header){
    // Read the full message
    LoginResponse response;
    response.header.msgSize = htons(sizeof(LoginResponse));
    response.header.msgType = 1;
    response.header.reqId = header.reqId;
    response.status = validStatus ? 1 : 0;

    //------------------------------------- Debug ----------------------------------
    std::cout << "[Login Response]"
          << "\n  Size:    " << sizeof(LoginResponse) << " bytes (network: 0x" << std::hex << response.header.msgSize << std::dec << ")"
          << "\n  Type:    " << static_cast<int>(response.header.msgType) << " (Response)"
          << "\n  ReqID:   " << static_cast<int>(response.header.reqId)
          << "\n  Status:  " << (response.status ? "SUCCESS" : "FAILURE")
          << std::endl;
    //------------------------------------- Debug ----------------------------------

    if(send(clientFD, &response, sizeof(response), MSG_NOSIGNAL) != sizeof(response)){
        throw std::runtime_error("Failed to send Login Response");
    }
    else{
        // Verify socket state
        int sendBufSize = 0;
        socklen_t len = sizeof(sendBufSize);
        getsockopt(clientFD, SOL_SOCKET, SO_SNDBUF, &sendBufSize, &len);
        std::cout << "Send buffer size: " << sendBufSize << " bytes\n";

        // Check for errors
        int socketError = 0;
        getsockopt(clientFD, SOL_SOCKET, SO_ERROR, &socketError, &len);
        if(socketError){
            std::cerr << "Socket error: " << strerror(socketError) << "\n";
        }
        else {
            std::cout << "Socket healthy, data should be sent\n";
        }
    }
};

bool SocketServer::handleLoginRequest(int clientFD, const MessageHeader& header){
    // Read the full message
    LoginRequest request;
    request.header = header;
    ssize_t bytes = recv(clientFD, &request.username, sizeof(request.username) + sizeof(request.password), MSG_WAITALL);
    
    if(bytes != sizeof(request.username) + sizeof(request.password)){
        std::cerr << std::endl << "Failed to read full payload";
        return false;
    }

    // Process the request
    request.username[sizeof(request.username)-1] = '\0';
    request.password[sizeof(request.password)-1] = '\0';
    
    std::string expectedUsername = "testuser";
    std::string expectedPassword = "testpass";
    std::string clientUsername = request.username;
    std::string clientPassword = request.password;

    // Compute CRC32 checksums
    uint32_t serverUsernameChecksum = crc32(expectedUsername);
    uint32_t clientUsernameChecksum = crc32(clientUsername);
    uint32_t serverPasswordChecksum = crc32(expectedPassword);
    uint32_t clientPasswordChecksum = crc32(clientPassword);

    //----------------------------------------------------- Debug -----------------------------------------------------
    // std::cout << std::endl << "ServerUsername: '" << expectedUsername << "' CRC32: 0x" << std::hex << serverUsernameChecksum;
    // std::cout << std::endl << "ClientUsername: '" << clientUsername << "' CRC32: 0x" << std::hex << clientUsernameChecksum;
    // std::cout << std::endl << "ServerPassword: '" << expectedPassword << "' CRC32: 0x" << std::hex << serverPasswordChecksum;
    // std::cout << std::endl << "ClientPassword: '" << clientPassword << "' CRC32: 0x" << std::hex << clientPasswordChecksum;
    //----------------------------------------------------- Debug -----------------------------------------------------

    // Compare checksums
    if((serverUsernameChecksum == clientUsernameChecksum) && (serverPasswordChecksum == clientPasswordChecksum)){
        std::cout << std::endl << "Success: Username and password matches" << std::endl;
        return true;
    }
    else if((serverUsernameChecksum != clientUsernameChecksum)){
        std::cout << std::endl << "Failed: Username mismatch" << std::endl;
        return false;
    }
    else if((serverPasswordChecksum != clientPasswordChecksum) ){
        std::cout << std::endl << "Failed: Password mismatch" << std::endl;
        return false;
    }
    else{
        std::cout << std::endl << "Failed: Username CRC32 mismatch (error detected!)." << std::endl;
        return false;
    }
};

void SocketServer::handleClientMessage(int clientFD) {
    // Read the header
    MessageHeader header;
    ssize_t bytes = recv(clientFD, &header, sizeof(header), MSG_WAITALL);
    
    if(bytes <= 0){
        if(bytes == 0){
            std::cout << std::endl << "Client disconnected gracefully";
        }
        else {
            if(errno == ECONNRESET){
                std::cout << std::endl << "Client " << clientFD << " disconnected abruptly (connection reset)";
            } 
            else{
                std::cerr << std::endl << "recv() error: " << strerror(errno);
            }
        }
        handleClientDisconnect(clientFD);
        return;
    }

    // ----------------------------- Debug -----------------------------
    // std::cout << std::endl << "header.msgSize: " << htons(header.msgSize);
    // std::cout << std::endl << "header.msgType: " << static_cast<int>(header.msgType);
    // std::cout << std::endl << "header.reqId: " << static_cast<int>(header.reqId);
    // ----------------------------- Debug -----------------------------

    bool responseStatus;
    switch(static_cast<int>(header.msgType)){
        case 0: // Handle login request and login response
            responseStatus = handleLoginRequest(clientFD, header);
            // std::cout << std::endl << "responseStatus: " << responseStatus << std::flush;
            handleLoginResponse(clientFD, responseStatus, header);
            break;
        case 2:
            std::cout << "\ntype2" <<std::flush;
            break;        
        default:
            std::cout << "\tNo type" <<std::flush;
            break;
    }
}

void SocketServer::run(){
    int clientFD{-1};
    
    while(true){
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(serverFD, &readfds);
        
        // Conditionally add client socket to monitoring set
        if (clientFD != -1) {
            FD_SET(clientFD, &readfds);
        }

        // Set 1s timeout for select() to prevent blocking indefinitely
        timeval tv{.tv_sec = 1, .tv_usec = 0};
        
        // Dynamically adjust this based on active connections.
        int nMaxFD = clientFD != -1 ? std::max(serverFD, clientFD) : serverFD;
        
        int activity = select(nMaxFD + 1, &readfds, nullptr, nullptr, &tv);
        if(activity < 0 && errno != EBADF){
            throw std::system_error(errno, std::generic_category(), "select() failed");
        }

        // Handle new connection
        if(FD_ISSET(serverFD, &readfds)){
            sockaddr_in clientAddr{};
            socklen_t addrLen = sizeof(clientAddr);
                        
            // Accept the pending connection
            clientFD = accept(serverFD, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
            if(clientFD < 0){
                throw std::system_error(errno, std::generic_category(), "accept() failed");
            }
            
            // Log connection details
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, client_ip, sizeof(client_ip));
            std::cout << std::endl << "New connection: " << client_ip << " (FD: " << clientFD << ")" << std::flush;
            
            // Send welcome message
            constexpr std::string_view welcome = "This is server. Connection established";
            send(clientFD, welcome.data(), welcome.size(), 0);
        }

        // Existing client communication
        if (clientFD != -1 && FD_ISSET(clientFD, &readfds)) {
            handleClientMessage(clientFD); // Delegate message processing
        }
    }
}

int main(){
    try{
        SocketServer server(8080);   
        server.run();
    }
    catch(const std::exception& e){
        std::cerr << std::endl << "Server error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}