#include "socket.hpp"

SocketClient::SocketClient(const std::string& host, uint16_t port)
    : host(host), port(port){
    clientFD = ::socket(AF_INET, SOCK_STREAM, 0);
    if(clientFD == -1)
        throw std::system_error(errno, std::generic_category(), "socket() failed");
    std::cout << "\nSuccess: Created socket " << clientFD;

    clientAddr.sin_family = AF_INET;
    clientAddr.sin_port   = htons(port);
    if(::inet_pton(AF_INET, host.c_str(), &clientAddr.sin_addr) <= 0)
        throw std::system_error(errno, std::generic_category(), "Invalid address");
    std::cout << "\nSuccess: Client configured for " << host << ":" << port;

    int flags = ::fcntl(clientFD, F_GETFL, 0);
    if(flags == -1){
        ::close(clientFD);
        throw std::system_error(errno, std::generic_category(), "fcntl(F_GETFL) failed");
    }
    if(::fcntl(clientFD, F_SETFL, flags & ~O_NONBLOCK) == -1){
        ::close(clientFD);
        throw std::system_error(errno, std::generic_category(), "fcntl(F_SETFL) failed");
    }
    std::cout << "\nSuccess: Set to blocking mode";

    // Zero timeout disables SO_RCVTIMEO (no timeout)
    timeval tv{.tv_sec = 0, .tv_usec = 0};
    if(::setsockopt(clientFD, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1){
        ::close(clientFD);
        throw std::system_error(errno, std::generic_category(), "setsockopt(SO_RCVTIMEO) failed");
    }
    std::cout << "\nSuccess: Set socket options";

    if(::connect(clientFD, reinterpret_cast<sockaddr*>(&clientAddr), sizeof(clientAddr)) == -1){
        ::close(clientFD);
        throw std::system_error(errno, std::generic_category(), "connect() failed");
    }

    connected = true;
    std::cout << "\nSuccess: Connected to server at " << host << ":" << port;

    uint8_t sReqID;
    ssize_t bytes = ::recv(clientFD, &sReqID, sizeof(sReqID), 0);
    if(bytes < 0)
        throw std::system_error(errno, std::generic_category(), "recv() failed");
    if(bytes == 0){
        disconnect();
        throw std::runtime_error("Connection closed by server");
    }
    currentReqID = sReqID;
    std::cout << "\n";
}

SocketClient::~SocketClient(){
    disconnect();
}

void SocketClient::disconnect(){
    if(clientFD != -1){
        ::close(clientFD);
        clientFD  = -1;
        connected = false;
        std::cout << "\nDisconnected from server\n";
    }
}

void SocketClient::handleLoginRequest(const std::string& username, const std::string& password){
    LoginRequest req{};
    req.header.msgSize = htons(sizeof(LoginRequest));
    req.header.msgType = 0;
    req.header.reqId   = currentReqID;

    username.copy(req.username, sizeof(req.username) - 1);
    req.username[sizeof(req.username) - 1] = '\0';

    std::string passwordHash = sha256(password);
    passwordHash.copy(req.password, sizeof(req.password) - 1);
    req.password[sizeof(req.password) - 1] = '\0';

    std::string maskedPw(password.size(), '*');
    if(!password.empty()) maskedPw[0] = password[0];

    std::cout << "\n---------------------------------"
              << "\n[Sending Login Request to server]"
              << "\n  Size:      " << sizeof(LoginRequest) << " bytes"
              << "\n  Type:      " << static_cast<int>(req.header.msgType)
              << "\n  RequestID: " << static_cast<int>(req.header.reqId)
              << "\n  Username:  '" << req.username << "'"
              << "\n  Password:  '" << maskedPw << "'"
              << "\n---------------------------------";

    if(send(clientFD, &req, sizeof(req), MSG_NOSIGNAL) != sizeof(req))
        throw std::runtime_error("Failed to send login request");
}

bool SocketClient::handleLoginResponse(){
    LoginResponse response{};
    ssize_t received = recv(clientFD, &response, sizeof(response), MSG_WAITALL);
    if(received != sizeof(response)){
        if(received == -1)
            throw std::system_error(errno, std::generic_category(), "recv() failed");
        throw std::runtime_error("Incomplete login response");
    }

    uint16_t status = ntohs(response.status);

    std::cout << "\n---------------------------------"
              << "\n[Received Login Response from server]"
              << "\n  Size:   " << ntohs(response.header.msgSize) << " bytes"
              << "\n  Type:   " << static_cast<int>(response.header.msgType)
              << "\n  ReqID:  " << static_cast<int>(response.header.reqId)
              << "\n  Status: " << (status == 1 ? "SUCCESS" : "FAILURE")
              << "\n---------------------------------";

    if(status == 1){
        std::cout << "\nLogin Success!\n";
        return true;
    }
    std::cout << "\nLogin Failed. Try again\n";
    disconnect();
    return false;
}

int main(){
    try{
        std::string username, password, input;
        SocketClient client("127.0.0.1", 8080);

        std::cout << "\nLogin: Please enter username and password:-"
                  << "\nUsername:";
        std::getline(std::cin, username);
        std::cout << "Password:";
        std::getline(std::cin, password);

        client.handleLoginRequest(username, password);
        bool loggedIn = client.handleLoginResponse();

        if(loggedIn){
            while(true){
                std::cout << "\nEnter message to send (or 'exit' to quit): ";
                if(!std::getline(std::cin, input) || input == "exit")
                    break;
            }
            client.disconnect();
        }
    }
    catch(const std::exception& e){
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
