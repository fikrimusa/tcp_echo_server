#include "socket.hpp"

SocketClient::SocketClient(const std::string &host, uint16_t port)
    : host(host), port(port){
    // Create socket
    clientFD = ::socket(AF_INET, SOCK_STREAM, 0);
    if(clientFD == -1){
        throw std::system_error(errno, std::generic_category(), "socket() failed");
    }    
    else{
        std::cout << std::endl << "Success: Created socket " << clientFD;
    }
    
    // Client configurations
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_port = htons(port);
    if(::inet_pton(AF_INET, host.c_str(), &clientAddr.sin_addr) <= 0){
        throw std::system_error(errno, std::generic_category(), "Invalid address");
    }
    else{
        std::cout << std::endl << "Success: Client configured for " << host << ":" << port;
    }

    // Set socket to blocking mode
    int flags = ::fcntl(clientFD, F_GETFL, 0);
    if(flags == -1){
        ::close(clientFD);
        throw std::system_error(errno, std::generic_category(), "fcntl(F_GETFL) failed");
    }

    if(::fcntl(clientFD, F_SETFL, flags & ~O_NONBLOCK) == -1){
        ::close(clientFD);
        throw std::system_error(errno, std::generic_category(), "fcntl(F_SETFL) failed");
    }
    else{
        std::cout << std::endl << "Success: Set to blocking mode";
    }

    // Timeout
    std::chrono::milliseconds timeout;
    timeval tv{
        .tv_sec = static_cast<long>(timeout.count() / 1000),
        .tv_usec = static_cast<long>((timeout.count() % 1000) * 1000)
    };
    if(::setsockopt(clientFD, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1){
        throw std::system_error(errno, std::generic_category(), "setsockopt(SO_RCVTIMEO) failed");
    }
    else{
        std::cout << std::endl << "Success: Set socket options";
    }

    // Connect to server
    if(::connect(clientFD, reinterpret_cast<sockaddr*>(&clientAddr), sizeof(clientAddr)) == -1){
        ::close(clientFD);
        throw std::system_error(errno, std::generic_category(), "connect() failed");
    }
    else{
        connected = true;
        std::cout << std::endl << "Success: Connected to server at " << host << ":" << port;
    
        // Receive from server
        std::array<char, BUFFERSIZE> buffer{};
        ssize_t bytes = ::recv(clientFD, buffer.data(), buffer.size() - 1, 0);
        // std::cout << std::endl << bytes;
        if(bytes < 0){
            throw std::system_error(errno, std::generic_category(), "recv() failed");
        }
        if(bytes == 0){
            disconnect();
            throw std::runtime_error("Connection closed by server");
        }    
        buffer[bytes] = '\0';

        std::string response(buffer.data(), bytes);
        std::cout << std::endl << response << std::endl << std::flush;
    }
}

SocketClient::~SocketClient(){
    disconnect();
}

void SocketClient::disconnect(){
    if(clientFD != -1){
        ::close(clientFD);
        clientFD = -1;
        connected = false;
        std::cout << std::endl << "Disconnected from server" << std::endl;
    }
}

bool SocketClient::handleLoginResponse(){
    uint8_t raw[6];
    
    ssize_t received = recv(clientFD, raw, sizeof(raw), 0);
    if (received != sizeof(raw)) {
        if (received == -1) {
            throw std::system_error(errno, std::generic_category(), "recv() failed");
        }
        throw std::runtime_error("Incomplete login response");
    }

    // Parse with network byte order
    uint16_t msgSize = ntohs(*reinterpret_cast<uint16_t*>(&raw[0]));
    uint8_t msgType = raw[2];
    uint8_t reqId = raw[3];
    uint16_t status = ntohs(*reinterpret_cast<uint16_t*>(&raw[4]));

    std::cout << "Raw bytes: ";
    for (int i = 0; i < 6; i++) {
        printf("%02X ", raw[i]);
    }

    std::cout << "\n[Login Response]"
              << "\n  Size:   " << msgSize << " bytes"
              << "\n  Type:   " << static_cast<int>(msgType)
              << "\n  ReqID:  " << static_cast<int>(reqId)
              << "\n  Status: " << (status == 0x0100 ? "SUCCESS" : "FAILURE") 
              << std::endl;

    if(status == 0x0100){
        std::cout << std::endl << "Login Success!" << std::endl;
        return 1;
    }
    else{
        std::cout << std::endl << "Login Failed. Try again" << std::endl;
        disconnect();
        return 0;
    }
}

void SocketClient::handleLoginRequest(const std::string &username, const std::string &password){
    LoginRequest req{};
    req.header.msgSize= htons(sizeof(LoginRequest));
    req.header.msgType = 0;
    req.header.reqId = 30;

    // Safe string copying with null termination
    username.copy(req.username, sizeof(req.username) - 1);
    req.username[username.length() < sizeof(req.username) ? username.length() : sizeof(req.username) - 1] = '\0';
    password.copy(req.password, sizeof(req.password) - 1);
    req.password[password.length() < sizeof(req.password) ? password.length() : sizeof(req.password) - 1] = '\0';

    //------------------------------------- Debug ----------------------------------
    std::cout << "[Login Request]"
              << "\n  Size:      " << sizeof(LoginRequest) << " bytes"
              << "\n  Type:      " << static_cast<int>(req.header.msgType) << " (Login)"
              << "\n  RequestID: " << req.header.reqId
              << "\n  Username:  '" << req.username << "'"
              << "\n  Password:  '" << std::string(req.password).replace(1, std::string::npos, strlen(req.password)-1, '*') << "'"
              << std::endl;
   //------------------------------------- Debug ----------------------------------

    if(send(clientFD, &req, sizeof(req), MSG_NOSIGNAL) != sizeof(req)){
        throw std::runtime_error("Failed to send login request");
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
}

int main(){
    try{
        bool loginResponse;
        std::string input, username, password;
        SocketClient client("127.0.0.1", 8080);
        
        std::cout << std::endl << "Login. Please enter username and password";
        std::cout << std::endl << "Username:";
        std::getline(std::cin, username);
        std::cout << "Password:";
        std::getline(std::cin, password);
      
        client.handleLoginRequest(username,password);
        loginResponse = client.handleLoginResponse();
        
        if(loginResponse){
            while(true){
                std::cout << std::endl << "Enter message to send (or 'exit' to quit): ";
                if(!std::getline(std::cin, input)){
                    break;
                }

                if(input == "exit"){
                    break;
                }

                try{
                    // std::string statusMessage = client.receive();
                    // std::cout << statusMessage << std::endl;
                }
                catch(const std::exception& e){
                    std::cerr << "Error: " << e.what() << std::endl;
                    if(dynamic_cast<const std::runtime_error*>(&e)){
                        break;  // Break on connection errors
                    }
                }
            }
            client.disconnect();
        }
    }
    catch(const std::exception& e){
            std::cerr << "Error: " << e.what() << std::endl;
            return EXIT_FAILURE;
    }
        return EXIT_SUCCESS;
}