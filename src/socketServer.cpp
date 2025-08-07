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

constexpr auto generateCRCTable() {
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

uint32_t crc32(const std::string& str) {
    static constexpr auto crc_table = generateCRCTable();
    uint32_t crc = 0xFFFFFFFF;
    for (char c : str) {
        crc = (crc >> 8) ^ crc_table[(crc ^ static_cast<uint8_t>(c)) & 0xFF];
    }
    return ~crc;
}

// Convert a string to 16-bit words (big-endian)
std::vector<uint16_t> stringToWords(const std::string& str) {
    std::vector<uint16_t> words;
    for (size_t i = 0; i < str.size(); i += 2) {
        uint16_t word = (i + 1 < str.size()) 
                      ? (static_cast<uint16_t>(str[i]) << 8) | str[i + 1] 
                      : static_cast<uint16_t>(str[i]) << 8; // Pad with 0 if odd length
        words.push_back(word);
    }
    return words;
}

// Compute 16-bit checksum
uint16_t computeChecksum(const std::vector<uint16_t>& data) {
    uint32_t sum = 0;
    for (uint16_t word : data) {
        sum += word;
        if (sum > 0xFFFF) {
            sum = (sum & 0xFFFF) + 1; // Wrap around carry
        }
    }
    return static_cast<uint16_t>(~sum); // 1's complement
}

// Linear Congruential Generator (LCG) for key generation
uint32_t next_key(uint32_t key) {
    return (key * 1103515245 + 12345) % 0x7FFFFFFF;
}

constexpr uint8_t calculateChecksum(std::string_view str) {
    uint8_t sum = 0;
    for (unsigned char c : str) {
        sum += c;
    }
    return 255 - (sum % 256); // Alternative method
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

    //--------------- Debug --------------------
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
    // --------------- Debug --------------------

    // constexpr std::string_view expectedUsername = "testuser";
    // constexpr std::string_view expectedPassword = "testpass";

    // uint8_t usernameChecksum = calculateChecksum(expectedUsername);
    // uint8_t passwordChecksum = calculateChecksum(expectedPassword);
    // std::cout << " uCS: " << static_cast<int>(usernameChecksum) << std::flush;
    // std::cout << " pCS: " << static_cast<int>(passwordChecksum) << std::flush;
    // std::cout << " uCS: " << expectedUsername << std::flush;
    // std::cout << " pCS: " << expectedPassword << std::flush;



    // uint16_t messageSequence = 87; // Potential bug

    // Generate initial key (assuming message_sequence is available from request)
    // uint32_t initial_key = (messageSequence << 16) | (usernameChecksum << 8) | passwordChecksum;
    std::string expectedUsername = "testuser";
    std::string expectedPassword = "testpass";
    std::string clientUsername = request.username; // Assume this comes from a client

    // Compute CRC32 checksums
    uint32_t serverUsernameChecksum = crc32(expectedUsername);
    uint32_t clientUsernameChecksum = crc32(clientUsername);
    uint32_t serverPasswordChecksum = crc32(expectedPassword);

    // Output results
    std::cout << "\nServer '" << expectedUsername << "' CRC32: 0x" 
            << std::hex << serverUsernameChecksum << std::endl;
    std::cout << "Client '" << clientUsername << "' CRC32: 0x" 
            << std::hex << clientUsernameChecksum << std::endl;

    uint16_t validStatus = 0;
    // Compare checksums
    if (serverUsernameChecksum == clientUsernameChecksum) {
        std::cout << "Username CRC32 matches (no errors)." << std::endl;
        validStatus = 1;
    } else {
        std::cout << "Username CRC32 mismatch (error detected!)." << std::endl;
        validStatus = 0;
    }

    // Generate and print cipher keys
    // uint32_t current_key = initial_key;
    // for (int i = 0; i < 64; ++i) {
    //     current_key = next_key(current_key);
    //     uint8_t cipher_byte = current_key % 256;
        
    //     // Format output with uppercase hex and fixed width
    //     std::cout << std::uppercase << std::hex << std::setw(2) << std::setfill('0') 
    //               << static_cast<int>(cipher_byte) << " ";
        
    //     // New line every 16 bytes
    //     if ((i + 1) % 16 == 0) {
    //         std::cout << "\n";
    //     }
    // }    

    //if(std::string_view(request.username) == expectedUsername && std::string_view(request.password) == expectedPassword) {
    switch(static_cast<int>(request.header.msgType)){
        case 0: // Got login request information
            LoginResponse res;
            res.header.msgSize = htons(sizeof(LoginResponse));  // Ensure network byte order
            res.header.msgType = 1;                              // LoginResp type
            res.header.reqId = header.reqId;                     // Match request ID
            res.status = validStatus;                                      // 1 = OK, 0 = FAILED

            if (send(clientFD, &res, sizeof(res), MSG_NOSIGNAL) != sizeof(res)) {
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
                } else {
                    std::cout << "Socket healthy, data should be sent\n";
                }   
            }
            // sleep(3);
            std::cout << "\ntype0" <<std::flush;
            break;
        // case 1:
        //     std::cout << "\ntype1" <<std::flush;
        //     break;
        // case 2:
        //     std::cout << "\ntype2" <<std::flush;
        //     break;        
        default:
            std::cout << "\tNo type" <<std::flush;
            break;
    }

    //}
    // else{
    //     LoginResponse res;
    //     res.header.msgSize = htons(sizeof(LoginResponse));  // Ensure network byte order
    //     res.header.msgType = 1;                              // LoginResp type
    //     res.header.reqId = header.reqId;                     // Match request ID
    //     res.status = 0;                                      // 1 = OK, 0 = FAILED
    //     if (send(clientFD, &res, sizeof(res), MSG_NOSIGNAL) != sizeof(res)) {
    //         throw std::runtime_error("Failed to send Login Response");
    //     }
    //     handleClientDisconnect(clientFD);
    //     return;
    // }
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