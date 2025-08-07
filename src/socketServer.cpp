#include "socketServer.hpp"

SocketServer::SocketServer(uint16_t port){
    // Create socket
    serverFD = socket(AF_INET, SOCK_STREAM, 0);
    if(serverFD == -1){
        throw std::system_error(errno, std::system_category(), "socket() failed");
    }
    else{
        std::cout << std::endl << "Successfully created socket " << serverFD;
    }
    
    // Server configurations
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // Set socket options
    const int reuse = 1;
    if(setsockopt(serverFD, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))){
        ::close(serverFD);
        throw std::system_error(errno, std::system_category(), "setsockopt() failed");
    }
    else{
        std::cout << std::endl << "Successfully set socket options";
    }

    // Set socket to blocking mode
    int flags = fcntl(serverFD, F_GETFL, 0);
    if(flags == -1){
        ::close(serverFD);
        throw std::system_error(errno, std::system_category(), "fcntl(F_GETFL) failed");
    }

    if(fcntl(serverFD, F_SETFL, flags & ~O_NONBLOCK) == -1){
        ::close(serverFD);
        throw std::system_error(errno, std::system_category(), "fcntl(F_SETFL) failed");
    }
    else{
        std::cout << std::endl << "Socket successfully set to blocking mode";
    }
    
    // Bind with error handling
    if(::bind(serverFD, reinterpret_cast<const sockaddr*>(&serverAddr), sizeof(serverAddr)) == -1){
        ::close(serverFD);
        throw std::system_error(errno, std::system_category(), "bind() failed");
    }
    else{
        std::cout << std::endl << "Successfully bind";
    }

    // Listen with backlog
    if(listen(serverFD, 5)){
        ::close(serverFD);
        throw std::system_error(errno, std::system_category(), "listen() failed");
    }
    else{
        std::cout << std::endl << "Successfully listen ";
    }
    std::cout << std::endl << "Server listening on port " << port << std::endl;
}

SocketServer::~SocketServer() {
    if(serverFD != -1){
        close(serverFD);
        std::cout << "Server socket " << serverFD << " closed" << std::endl;
    }
}

void SocketServer::run() {
    int clientFD = -1;
    
    while(true){
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(serverFD, &readfds);
        
        // Only add client socket if it's valid
        if (clientFD != -1) {
            FD_SET(clientFD, &readfds);
        }

        timeval tv{.tv_sec = 1, .tv_usec = 0};
        int nMaxFD = clientFD != -1 ? std::max(serverFD, clientFD) : serverFD;
        
        int activity = select(nMaxFD + 1, &readfds, nullptr, nullptr, &tv);
        if(activity < 0 && errno != EBADF){
            throw std::system_error(errno, std::system_category(), "select() failed");
        }

        // Handle new connection
        if(FD_ISSET(serverFD, &readfds)){
            sockaddr_in clientAddr{};
            socklen_t addrLen = sizeof(clientAddr);
            
            // // Disconnect existing client if any
            // if(clientFD != -1){
            //     constexpr std::string_view msg = "Server is disconnecting you for new connection";
            //     send(clientFD, msg.data(), msg.size(), 0);
            //     handleClientDisconnect(clientFD);
            //     clientFD = -1;
            // }
            
            // Accept new connection
            clientFD = accept(serverFD, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
            if(clientFD < 0){
                throw std::system_error(errno, std::system_category(), "accept() failed");
            }
            
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, client_ip, sizeof(client_ip));
            std::cout << std::endl << "New connection: " << client_ip << " (FD: " << clientFD << ")" << std::flush;
            
            // Send welcome message
            constexpr std::string_view welcome = "This is server. Connection established";
            send(clientFD, welcome.data(), welcome.size(), 0);
        }

        // Handle client message
        if (clientFD != -1 && FD_ISSET(clientFD, &readfds)) {
            handleClientMessage(clientFD);
        }
    }
}

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

    // Convert network byte order
    header.msgSize = htons(header.msgSize);
    header.reqId = header.reqId;

    // Validate header
    if (header.msgSize != sizeof(LoginRequest)) {
        std::cerr << std::endl << "Invalid message size: " << header.msgSize << " (expected " << sizeof(LoginRequest) << ")";
        return;
    }

    // Read the full message
    LoginRequest request;
    memcpy(&request.header, &header, sizeof(header));
    
    // Read remaining payload (username + password)
    bytes = recv(clientFD, &request.username, sizeof(request.username) + sizeof(request.password), MSG_WAITALL);
    
    if (bytes != sizeof(request.username) + sizeof(request.password)) {
        std::cerr << std::endl << "Failed to read full payload";
        return;
    }

    // Process the request
    request.username[sizeof(request.username)-1] = '\0';
    request.password[sizeof(request.password)-1] = '\0';
    request.header.msgSize = htons(header.msgSize);

    // std::cout << "\n=== Received Login Request ==="
    //           << "\n  msgSize: " << request.header.msgSize
    //           << "\n  msgType: " << static_cast<int>(request.header.msgType)
    //           << "\n  reqId: " << static_cast<int>(header.reqId)
    //           << "\n  Username: " << request.username
    //           << "\n  Password: " << request.password << std::flush;

    // // Hex dump of the entire LoginRequest
    // std::cout << std::endl << "Hex dump (" << sizeof(LoginRequest) << " bytes):";

    // const uint8_t* raw_data = reinterpret_cast<const uint8_t*>(&request);
    // for(size_t i = 0; i < sizeof(LoginRequest); i++){
    //     if(i % 16 == 0) std::cout << "\n  ";
    //     std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(raw_data[i]) << " ";
    // }
    // std::cout << std::flush;
}

void SocketServer::handleClientDisconnect(int clientFD){
    if (clientFD == -1) return;
    std::cout << std::endl << "Client " << clientFD << " disconnected" << std::flush;
    close(clientFD);
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