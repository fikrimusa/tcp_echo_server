#pragma once
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <arpa/inet.h>
#include <array>
#include <system_error>
#include <chrono>
#include <sys/ioctl.h>
#include <iomanip>
#include <vector>
#include "messageProtocol.hpp"

// =================== SocketServer Class =============================
class SocketServer{
public:
    /* Constructor that:
    * 1. Creates a socket
    * 2. Sets socket options
    * 3. Configures blocking mode
    * 4. Binds the socket
    * 5. Starts listening
    */
    explicit SocketServer(uint16_t port);

    /* Destructor that close server socket */
    ~SocketServer();

    /**
     * Main server loop that handles incoming connections and client messages.
     * 
     * 1. Sets up a file descriptor set for select() monitoring (server + client sockets)
     * 2. Waits for activity with a 1-second timeout
     * 3. Handles new connections by:
     *    - Accepting incoming connections
     *    - Logging client IP information
     *    - Sending a welcome message
     * 4. Processes incoming client messages when available
     * 5. Throws system_error for socket-related failures
     *
     */
    void run();
    void handleClientMessage(int);

    /* * Handles a client login request by validating credentials against expected values.
    * 
    * This function:
    * 1. Receives and validates login credentials (username/password) from a client socket
    * 2. Performs CRC32 checksum verification for data integrity
    * 3. Returns authentication status
    * 
    * Workflow:
    * - Receives username and password payload from network
    * - Ensures null-termination of string fields
    * - Computes CRC32 checksums for both received and expected credentials
    * - Compares checksums to validate credentials
    * - Provides detailed failure diagnostics
    *
    * */
    bool handleLoginRequest(int, const MessageHeader&);
    void handleLoginResponse(int);
    void handleClientDisconnect(int);
    uint32_t crc32(const std::string &);
    static constexpr auto generateCRCTable();

private:
    static constexpr int MAXCLIENT = 3;
    int serverFD{-1};
    int nMaxFD{-1};
    std::array<int, MAXCLIENT> clients{};
    struct sockaddr_in serverAddr {};
    fd_set readfds, writefds, exceptfds;
};
// ====================================================================
// =================== SocketClient Class =============================
class SocketClient{
public:
    explicit SocketClient(const std::string&, uint16_t);
    ~SocketClient();
    void disconnect();
    void login(const std::string &username, const std::string &password);

    int getSocketFD() const { return clientFD; }
    static constexpr int getBufferSize() { return BUFFERSIZE; }
    void receive();

private:
    static constexpr int BUFFERSIZE = 256;
    int clientFD{-1};
    bool connected = false;
    std::string host;
    uint16_t port;
    sockaddr_in clientAddr{};
};
// ====================================================================