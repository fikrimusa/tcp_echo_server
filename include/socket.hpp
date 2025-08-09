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

    /**
     * Handles incoming client messages and routes them to appropriate handlers.
     * 
     * This function:
     * 1. Reads and validates message headers from client socket
     * 2. Routes messages based on message type
     * 3. Handles client disconnections (graceful/abrupt)
     * 
     * Message Handling Workflow:
     * 1. Receives MessageHeader (msgSize, msgType, reqId)
     * 2. Validates connection state
     * 3. Routes using switch-case:
     *    - Case 0: LoginRequest → LoginResponse
     *    - Case 2: (Reserved for future EchoRequest/Response)
     *    - Default: Unsupported message types
     * 
     */
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

    /**
     * Handles and sends a login response to the client after authentication processing.
     * 
     * This function:
     * 1. Constructs a login response message with authentication status
     * 2. Provides detailed debug output of the response packet
     * 3. Sends the response to the client socket
     * 4. Verifies transmission success and socket health
     * 
     * Response Structure:
     * - msgSize: Size of LoginResponse struct (network byte order)
     * - msgType: Set to 1 (login response)
     * - reqId: Mirrors the request ID from the client
     * - status: 1 (success) or 0 (failure)
     *
     * Network Protocol Notes:
     * - Uses MSG_NOSIGNAL to prevent SIGPIPE on broken pipes
     * - Maintains network byte order for cross-platform compatibility
     * - Ensures complete packet transmission (compares sent bytes vs expected)
     *
     */
    void handleLoginResponse(int, bool, const MessageHeader&);

    /**
     * Handles client disconnection cleanup in a thread-safe manner.
     * 
     * Responsibilities:
     * 1. Validates client file descriptor
     * 2. Logs disconnection event
     * 3. Safely closes socket connection
     * 4. Prevents double-close errors
     * 
     */
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