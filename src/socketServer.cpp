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

void SocketServer::run(){
    nMaxFD = serverFD;

    while(true){
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);

        FD_SET(serverFD, &readfds);
        FD_SET(serverFD, &exceptfds);

        // Add all active clients
        for(int i = 0; i < MAXCLIENT; i++){
            if(clients[i] > 0){
                FD_SET(clients[i], &readfds);
                if(clients[i] > nMaxFD){
                    nMaxFD = clients[i];
                }
            }
        }

        timeval tv{.tv_sec = 1, .tv_usec = 0};

        int activity = select(nMaxFD + 1, &readfds, nullptr, nullptr, &tv);
        if(activity < 0){
            throw std::system_error(errno, std::system_category(), "select() failed");
        }
        else{
            // Check for new connections
            if(FD_ISSET(serverFD, &readfds)){
                //handleNewConnection();
                sockaddr_in clientAddr{};
                socklen_t addrLen = sizeof(clientAddr);

                int clientFD = accept(serverFD, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
                if(clientFD < 0){
                    throw std::system_error(errno, std::system_category(), "accept() failed");
                }
                else{
                    std::cout << std::endl << "Successfully accept connection from " << clientFD;
                }

                // Find empty slot
                bool added = false;
                for(auto& fd : clients){
                    if(fd == 0){
                        fd = clientFD;
                        FD_SET(clientFD, &readfds);
                        nMaxFD = std::max(nMaxFD, clientFD);
                        
                        // Log connection
                        char client_ip[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &clientAddr.sin_addr, client_ip, sizeof(client_ip));
                        std::cout << std::endl << "New connection: " << client_ip << " (FD: " << clientFD << ")" << std::flush;
                        
                        // Send welcome
                        constexpr std::string_view welcome = "This is server. Connection established";
                        send(clientFD, welcome.data(), welcome.size(), 0);
                        
                        added = true;
                        break;
                    }
                }

                if(!added){
                    constexpr std::string_view msg = "Server full. Disconnecting.";
                    send(clientFD, msg.data(), msg.size(), 0);
                    ::close(clientFD);
                    std::cerr << std::endl << "Rejected connection - server full";
                }


            }

            // Check client sockets
            for(int i = 0; i < MAXCLIENT; i++){
                if(clients[i] > 0 && FD_ISSET(clients[i], &readfds)){
                    handleClientMessage(clients[i]);
                }
            }
        }
    }
}



void SocketServer::handleClientMessage(int clientFD){
    std::array<char, 256> buffer{};
    
    ssize_t bytes = recv(clientFD, buffer.data(), buffer.size() - 1, MSG_DONTWAIT);

    if(bytes == 0){
        // Client disconnected
        std::cout << std::endl << "Client " << clientFD << " disconnected gracefully";
        handleClientDisconnect(clientFD);
        return;
    }
    else if(bytes < 0){
        // Handle different error cases
        if(errno == ECONNRESET){
            std::cout << std::endl << "Client " << clientFD << " disconnected abruptly (connection reset)";
        } 
        else if(errno == EAGAIN || errno == EWOULDBLOCK){
            // Shouldn't happen with blocking sockets unless you changed the mode
            return;
        }
        else{
            std::cerr << "\nrecv() error: " << strerror(errno) << std::endl;
        }
        handleClientDisconnect(clientFD);
        return;
    } 
    else{
        // Null-terminate and process
        buffer[bytes] = '\0';
        std::cout << "\nClient " << clientFD << ": " << buffer.data() << std::flush;

        // Echo response
        constexpr std::string_view response = "\nProcessed your request";
        if(send(clientFD, response.data(), response.size(), 0) < 0){
            throw std::system_error(errno, std::system_category(), "send() failed");
        }
    }    
}

// Helper method
void SocketServer::handleClientDisconnect(int clientFD){
    std::cout << std::endl << "Client " << clientFD << " disconnected" << std::endl;
    ::close(clientFD);
    
    for(auto& fd : clients){
        if(fd == clientFD){
            fd = 0;  // Mark slot as available
            FD_CLR(clientFD, &readfds);
            break;
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