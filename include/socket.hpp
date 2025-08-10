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
#include <atomic>
#include <thread>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <openssl/evp.h>
#include <sstream>
#include "messageProtocol.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

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

    /**
     * Calculates the CRC-32 checksum of a string using the precomputed lookup table.
     * 
     * Features:
     * - Uses static constexpr lookup table for O(1) per-byte calculation
     * - Processes strings of any length
     * - Final result is bitwise NOT (~) for standard CRC-32 output
     * 
     */
    uint32_t crc32(const std::string &);

    /**
     * Generates a static CRC-32 lookup table using polynomial 0xEDB88320 (standard for Ethernet, ZIP, etc.).
     * 
     * The table is generated at compile-time (constexpr) for optimal performance.
     * 
     */
    static constexpr auto generateCRCTable();

    /** 
    *  Launches a background thread that monitors console input for the "exit" command, allowing graceful server shutdown without using signals.
    *
    *  Key Responsibilities:
    *  1. Runs in a separate thread to avoid blocking the main server loop.
    *  2. Listens for user input from stdin.
    *  3. Triggers server shutdown when "exit" is entered.
    *  4. Wakes up the main select() loop via pipe when shutdown is requested.
    */
    void startConsoleListener();

private:
    static constexpr int MAXCLIENT = 3;
    int serverFD{-1};
    int nMaxFD{-1};
    std::array<int, MAXCLIENT> clients{};
    struct sockaddr_in serverAddr {};
    fd_set readfds, writefds, exceptfds;
    std::thread consoleThread;
    bool running;
    int wakeupFD;
};
// ====================================================================
// =================== SocketClient Class =============================
class SocketClient{
public:
    /**
     * Creates and configures a TCP client socket connection to a server.
     * 
     * Key Operations:
     * 1. Socket Creation:
     *    - Creates IPv4 TCP socket (AF_INET/SOCK_STREAM)
     *    - Throws on failure with errno details
     *
     * 2. Address Configuration:
     *    - Converts host string to network address (inet_pton)
     *    - Sets port in network byte order (htons)
     *
     * 3. Socket Settings:
     *    - Forces blocking mode (clears O_NONBLOCK)
     *    - Configures receive timeout (SO_RCVTIMEO)
     *
     * 4. Connection Establishment:
     *    - Initiates TCP handshake (connect())
     *    - Immediately verifies connection via test receive
     *
     * 5. Resource Cleanup:
     *    - Closes socket on any failure
     *    - Sets 'connected' flag only on full success
     *
     */
    explicit SocketClient(const std::string&, uint16_t);

    /**
     * Ensures proper socket cleanup when client object is destroyed.
     * 
     * Behavior:
     * - Delegates to disconnect() for actual cleanup
     * - Guarantees no resource leaks
     * - Maintains noexcept safety (indirectly via disconnect())
     * 
     * Important Notes:
     * - Must not throw exceptions (matches disconnect() behavior)
     * - Safe for stack and heap allocated objects
     * - May block briefly during socket closure
     * 
     */
    ~SocketClient();

    /**
     * Gracefully closes the client socket connection and cleans up resources.
     * 
     * Features:
     * - Idempotent operation (safe to call multiple times)
     * - Closes socket file descriptor if valid (!= -1)
     * - Updates connection state flag
     * - Logs disconnection event
     * 
     * Safety Mechanisms:
     * - Checks for valid file descriptor before closing
     * - Atomic state update (FD + connected flag)
     * - Uses global scope ::close() to avoid ambiguity
     * 
     */
    void disconnect();

    /**
     * Sends a login request to the server with credentials.
     * 
     * Protocol Details:
     * - Uses fixed-size LoginRequest structure
     * - Message Type: 0 (login request)
     * - Hardcoded Request ID
     * - Network byte order for msgSize (htons)
     *
     * Safety Features:
     * - Ensures null-termination of credentials
     * - Validates complete message transmission
     * - Prevents buffer overflows with length checks
     *
     * Data Handling:
     * - Truncates credentials exceeding max size:
     *   - Username max: sizeof(req.username) - 1
     *   - Password max: sizeof(req.password) - 1
     * - Always null-terminates copied strings
     *
     */
    void handleLoginRequest(const std::string &username, const std::string &password);

    /**
     * Sends a login request to the server with credentials.
     * 
     * Protocol Details:
     * - Uses fixed-size LoginRequest structure
     * - Message Type: 0 (login request)
     * - Hardcoded Request ID
     * - Network byte order for msgSize (htons)
     *
     */
    bool handleLoginResponse();

private:
    static constexpr int BUFFERSIZE = 256;
    int clientFD{-1};
    bool connected = false;
    std::string host;
    uint16_t port;
    sockaddr_in clientAddr{};
};
// ====================================================================