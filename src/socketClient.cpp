#include "socketServer.hpp"

SocketClient::SocketClient(const std::string &host, uint16_t port)
    : host(host), port(port){
    // Create socket
    clientFD = socket(AF_INET, SOCK_STREAM, 0);
    if(clientFD == -1){
        throw std::system_error(errno, std::system_category(), "socket() failed");
    }    
    else{
        std::cout << std::endl << "Successfully created socket " << clientFD;
    }
    
    // Client configurations
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_port = htons(port);
    if(inet_pton(AF_INET, host.c_str(), &clientAddr.sin_addr) <= 0){
        throw std::system_error(errno, std::system_category(), "Invalid address");
    }
    else{
        std::cout << std::endl << "Client configured for " << host << ":" << port;
    }

    // Set socket to blocking mode
    int flags = fcntl(clientFD, F_GETFL, 0);
    if(flags == -1){
        ::close(clientFD);
        throw std::system_error(errno, std::system_category(), "fcntl(F_GETFL) failed");
    }

    if(fcntl(clientFD, F_SETFL, flags & ~O_NONBLOCK) == -1){
        ::close(clientFD);
        throw std::system_error(errno, std::system_category(), "fcntl(F_SETFL) failed");
    }
    else{
        std::cout << std::endl << "Socket successfully set to blocking mode";
    }

    // Timeout
    std::chrono::milliseconds timeout;
    timeval tv{
        .tv_sec = static_cast<long>(timeout.count() / 1000),
        .tv_usec = static_cast<long>((timeout.count() % 1000) * 1000)
    };
    if(setsockopt(clientFD, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1){
        throw std::system_error(errno, std::system_category(), "setsockopt(SO_RCVTIMEO) failed");
    }

    // Connect to server
    if(connect(clientFD, reinterpret_cast<sockaddr*>(&clientAddr), sizeof(clientAddr)) == -1){
        ::close(clientFD);
        throw std::system_error(errno, std::system_category(), "connect() failed");
    }
    else{
        connected = true;
        std::cout << std::endl << "Successfully connected to server at " << host << ":" << port;
    
        // Receive from server
        std::array<char, BUFFERSIZE> buffer{};
        ssize_t bytes = recv(clientFD, buffer.data(), buffer.size() - 1, 0);
        std::cout << bytes;
        if(bytes < 0){
            throw std::system_error(errno, std::system_category(), "recv() failed");
        }
        if(bytes == 0){
            disconnect();
            throw std::runtime_error("Connection closed by server");
        }    
        buffer[bytes] = '\0';

        std::string response(buffer.data(), bytes);
        std::cout << std::endl << response << std::flush;
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

void SocketClient::login(const std::string &username, const std::string &password){
    LoginRequest req{};
    req.header ={
        .msgSize = htons(sizeof(LoginRequest)),
        .msgType = 25,
        .reqId = 2
    };

    // Copy username
    username.copy(req.username, sizeof(req.username) - 1);
    req.username[username.length() < sizeof(req.username) ? username.length() : sizeof(req.username) - 1] = '\0';

    // Copy password
    password.copy(req.password, sizeof(req.password) - 1);
    req.password[password.length() < sizeof(req.password) ? password.length() : sizeof(req.password) - 1] = '\0';

    // --- Debug Output ---
    std::cout << "\n=== Sending LoginRequest ===";
    std::cout << "\nHeader:";
    std::cout << "\n  msgSize: " << ntohs(req.header.msgSize);
    std::cout << "\n  msgType: " << static_cast<int>(req.header.msgType);
    std::cout << "\n  reqId: " << static_cast<int>(req.header.reqId);
    std::cout << "\nCredentials:";
    std::cout << "\n  Username: " << req.username;
    std::cout << "\n  Password: " << req.password;
    std::cout << "\nHex Dump:";

    // Print raw bytes
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&req);
    for(size_t i = 0; i < sizeof(req); i++){
        if(i % 16 == 0) std::cout << "\n  ";
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]) << " ";
    }
    std::cout << "\n==========================\n";

    if(send(clientFD, &req, sizeof(req), MSG_NOSIGNAL) != sizeof(req)){
        throw std::runtime_error("Failed to send login request");
    }
    else{
        std::cout << "Done send login request to server" << std::flush;
        std::cout << "\nClient sent " << sizeof(req) << " bytes. Checking socket...\n";

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
}

int main(){
    try{
        SocketClient client("127.0.0.1", 8080);
        client.login("testusername","testpassword");

        std::string input;
        while(true){
            std::cout << std::endl << "Enter message to send (or 'exit' to quit): ";
            if(!std::getline(std::cin, input)){
                break;
            }

            if(input == "exit"){
                break;
            }

            try{
                // client.send(input);
                // auto response = client.receive();
                // std::cout << std::endl << "Server response: " << response;
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
    catch(const std::exception& e){
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}