#include "socket.hpp"

SocketServer::SocketServer(uint16_t port){
    int pipefd[2];
    if(pipe(pipefd) == -1)
        throw std::system_error(errno, std::generic_category(), "pipe() failed");
    wakeupReadFD  = pipefd[0];
    wakeupWriteFD = pipefd[1];

    serverFD = ::socket(AF_INET, SOCK_STREAM, 0);
    if(serverFD == -1){
        close(wakeupReadFD); close(wakeupWriteFD);
        throw std::system_error(errno, std::generic_category(), "socket() failed");
    }
    std::cout << "\nSuccess: Created socket " << serverFD;

    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_port        = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    const int reuse = 1;
    if(::setsockopt(serverFD, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))){
        ::close(serverFD); close(wakeupReadFD); close(wakeupWriteFD);
        throw std::system_error(errno, std::generic_category(), "setsockopt() failed");
    }
    std::cout << "\nSuccess: Set socket options";

    int flags = ::fcntl(serverFD, F_GETFL, 0);
    if(flags == -1){
        ::close(serverFD); close(wakeupReadFD); close(wakeupWriteFD);
        throw std::system_error(errno, std::generic_category(), "fcntl(F_GETFL) failed");
    }
    if(::fcntl(serverFD, F_SETFL, flags & ~O_NONBLOCK) == -1){
        ::close(serverFD); close(wakeupReadFD); close(wakeupWriteFD);
        throw std::system_error(errno, std::generic_category(), "fcntl(F_SETFL) failed");
    }
    std::cout << "\nSuccess: Set to blocking mode";

    if(::bind(serverFD, reinterpret_cast<const sockaddr*>(&serverAddr), sizeof(serverAddr)) == -1){
        ::close(serverFD); close(wakeupReadFD); close(wakeupWriteFD);
        throw std::system_error(errno, std::generic_category(), "bind() failed");
    }
    std::cout << "\nSuccess: Bind";

    if(::listen(serverFD, 5)){
        ::close(serverFD); close(wakeupReadFD); close(wakeupWriteFD);
        throw std::system_error(errno, std::generic_category(), "listen() failed");
    }
    std::cout << "\nSuccess: Listen";

    clients.fill(-1);
    std::cout << "\n\nServer listening on port " << port << "...\n\n";
}

SocketServer::~SocketServer(){
    running = false;
    if(wakeupWriteFD != -1){
        char dummy = 0;
        write(wakeupWriteFD, &dummy, 1);
    }
    if(consoleThread.joinable())
        consoleThread.join();
    if(serverFD != -1)
        close(serverFD);
    if(wakeupReadFD  != -1) close(wakeupReadFD);
    if(wakeupWriteFD != -1) close(wakeupWriteFD);
}

void SocketServer::startConsoleListener(){
    consoleThread = std::thread([this]() {
        std::string input;
        while(running){
            std::getline(std::cin, input);
            if(input == "exit"){
                running = false;
                char dummy = 0;
                write(wakeupWriteFD, &dummy, 1);
                break;
            }
        }
    });
}

constexpr auto SocketServer::generateCRCTable(){
    std::array<uint32_t, 256> table{};
    for(uint32_t i = 0; i < 256; ++i){
        uint32_t crc = i;
        for(int j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        table[i] = crc;
    }
    return table;
}

uint32_t SocketServer::crc32(const std::string& str){
    static constexpr auto crc_table = generateCRCTable();
    uint32_t crc = 0xFFFFFFFF;
    for(char c : str)
        crc = (crc >> 8) ^ crc_table[(crc ^ static_cast<uint8_t>(c)) & 0xFF];
    return ~crc;
}

void SocketServer::handleClientDisconnect(int clientFD){
    if(clientFD == -1) return;
    std::cout << "\nClient " << clientFD << " disconnected\n" << std::flush;
    close(clientFD);
}

void SocketServer::handleLoginResponse(int clientFD, bool validStatus, const MessageHeader& header){
    LoginResponse response{};
    response.header.msgSize = htons(sizeof(LoginResponse));
    response.header.msgType = 1;
    response.header.reqId   = header.reqId;
    response.status         = htons(validStatus ? 1 : 0);

    std::cout << "\n---------------------------------"
              << "\n[Sending Login Response to client]"
              << "\n  Size:   " << sizeof(LoginResponse)
              << "\n  Type:   " << static_cast<int>(response.header.msgType)
              << "\n  ReqID:  " << static_cast<int>(response.header.reqId)
              << "\n  Status: " << (validStatus ? "SUCCESS" : "FAILURE")
              << "\n---------------------------------";

    if(send(clientFD, &response, sizeof(response), MSG_NOSIGNAL) != sizeof(response))
        throw std::runtime_error("Failed to send Login Response");
}

bool SocketServer::handleLoginRequest(int clientFD, const MessageHeader& header){
    LoginRequest request{};
    request.header = header;
    ssize_t bytes = recv(clientFD, &request.username, sizeof(request.username) + sizeof(request.password), MSG_WAITALL);

    if(bytes != static_cast<ssize_t>(sizeof(request.username) + sizeof(request.password))){
        std::cerr << "\nFailed to read full payload";
        return false;
    }

    request.username[sizeof(request.username) - 1] = '\0';
    request.password[sizeof(request.password) - 1] = '\0';

    fs::path jsonPath = "storage.json";
    if(!fs::exists(jsonPath))
        throw std::runtime_error("File not found: " + jsonPath.string());

    std::ifstream jsonFile(jsonPath);
    json data = json::parse(jsonFile);

    std::string clientUsername    = request.username;
    std::string clientPasswordHash = request.password;  // client sends SHA256 hex

    for(const auto& user : data["users"]){
        std::string storedUsername = user["username"];
        std::string storedHash     = user["password_hash"];

        if(crc32(storedUsername) == crc32(clientUsername) && storedHash == clientPasswordHash){
            std::cout << "\nSuccess: Credentials match\n";
            return true;
        }
    }

    std::cout << "\nFailed: No matching credentials\n";
    return false;
}

void SocketServer::handleClientMessage(int clientFD) {
    MessageHeader header{};
    ssize_t bytes = recv(clientFD, &header, sizeof(header), MSG_WAITALL);

    if(bytes <= 0){
        if(bytes == 0)
            std::cout << "\nClient disconnected gracefully" << std::flush;
        else if(errno == ECONNRESET)
            std::cout << "\nClient " << clientFD << " disconnected abruptly" << std::flush;
        else
            std::cerr << "\nrecv() error: " << strerror(errno);
        return;
    }

    switch(static_cast<int>(header.msgType)){
        case 0: {
            bool status = handleLoginRequest(clientFD, header);
            handleLoginResponse(clientFD, status, header);
            break;
        }
        case 2:
            std::cout << "\ntype2" << std::flush;
            break;
        default:
            std::cout << "\tUnknown type" << std::flush;
            break;
    }
}

void SocketServer::run() {
    running = true;
    startConsoleListener();

    while(running){
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(serverFD, &readfds);
        FD_SET(wakeupReadFD, &readfds);

        int maxFD = std::max(serverFD, wakeupReadFD);
        for(int fd : clients){
            if(fd != -1){
                FD_SET(fd, &readfds);
                maxFD = std::max(maxFD, fd);
            }
        }

        timeval tv{.tv_sec = 0, .tv_usec = 100000};
        int activity = select(maxFD + 1, &readfds, nullptr, nullptr, &tv);

        if(FD_ISSET(wakeupReadFD, &readfds)){
            char dummy;
            read(wakeupReadFD, &dummy, 1);
            break;
        }

        if(!running) break;

        if(activity < 0){
            if(errno == EINTR || errno == EBADF) continue;
            throw std::system_error(errno, std::generic_category(), "select() failed");
        }

        // Accept new connection
        if(FD_ISSET(serverFD, &readfds)){
            sockaddr_in clientAddr{};
            socklen_t addrLen = sizeof(clientAddr);
            int newFD = accept(serverFD, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
            if(newFD < 0){
                if(errno == EWOULDBLOCK || errno == EAGAIN) continue;
                throw std::system_error(errno, std::generic_category(), "accept() failed");
            }

            int slot = -1;
            for(int i = 0; i < MAXCLIENT; ++i){
                if(clients[i] == -1){ slot = i; break; }
            }

            if(slot == -1){
                std::cout << "\nMax clients reached, rejecting connection\n";
                close(newFD);
            } else {
                clients[slot] = newFD;
                uint8_t reqID = generateUniqueReqID();
                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &clientAddr.sin_addr, client_ip, sizeof(client_ip));
                std::cout << "\nNew connection: " << client_ip << " (FD: " << newFD << ")" << std::flush;

                if(send(newFD, &reqID, sizeof(reqID), MSG_NOSIGNAL) == -1){
                    handleClientDisconnect(newFD);
                    clients[slot] = -1;
                }
            }
        }

        // Service existing clients
        for(int i = 0; i < MAXCLIENT; ++i){
            int fd = clients[i];
            if(fd == -1 || !FD_ISSET(fd, &readfds)) continue;

            try {
                handleClientMessage(fd);
            } catch(const std::exception& e) {
                std::cerr << "\nClient handling error: " << e.what();
                handleClientDisconnect(fd);
                clients[i] = -1;
                continue;
            }

            // Detect disconnect after message handling
            char buf;
            int ret = recv(fd, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
            if(ret == 0 || (ret == -1 && errno != EAGAIN && errno != EWOULDBLOCK)){
                handleClientDisconnect(fd);
                clients[i] = -1;
            }
        }
    }

    for(int i = 0; i < MAXCLIENT; ++i){
        if(clients[i] != -1){
            handleClientDisconnect(clients[i]);
            clients[i] = -1;
        }
    }
}

int main(){
    try{
        SocketServer server(8080);
        std::cout << "Type 'exit' and press Enter to shutdown\n";
        server.run();
        std::cout << "Server shutdown\n";
        return EXIT_SUCCESS;
    }
    catch(const std::exception& e){
        std::cerr << "\nServer error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
