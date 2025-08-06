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

// ==================== Message Protocol Structures ====================
#pragma pack(push, 1)  // Ensure no padding in structs

struct MessageHeader{
    uint16_t msgSize;  // Total message size (including header)
    uint8_t msgType;   // 0=LoginReq, 1=LoginResp, 2=EchoReq
    uint8_t reqId;     // Sequence number
};

struct LoginRequest{
    MessageHeader header;
    char username[32];  // Null-padded fixed-length string
    char password[32];  // Null-padded fixed-length string
};

struct LoginResponse{
    MessageHeader header;
    uint16_t status;    // 0=FAILED, 1=OK
};

struct EchoRequest{
    MessageHeader header;
    uint16_t msgSize;  // Size of cipher message only
    char ciphertext[];
    // Cipher message data follows here (variable length)
};

struct EchoResponse{
    MessageHeader header;
    uint16_t msgSize;  // Size of plain message only
    char message[];
    // Plain message data follows here (variable length)
};

#pragma pack(pop)  // Restore default packing
// ====================================================================
// =================== SocketServer & SocketClient Class ==============
class SocketServer{
public:
    explicit SocketServer(uint16_t port);
    ~SocketServer();

    // Move semantics
    SocketServer(SocketServer &&other) noexcept;
    SocketServer &operator=(SocketServer&& other) noexcept;

    // Explicitly delete copy operations
    SocketServer(const SocketServer&) = delete;
    SocketServer& operator=(const SocketServer&) = delete;

    void reset(int new_serverFD = -1) noexcept;
    void run();
    void handleNewConnection();
    void handleClientMessage(int);
    void handleClientDisconnect(int);

    // Explicit state check
    bool is_valid() const noexcept { return serverFD != -1; }

    // Safe accessor
    int native_handle() const noexcept { return serverFD; }

private:
    static constexpr int MAXCLIENT = 3;
    int serverFD{-1};
    int nMaxFD{-1};
    std::array<int, MAXCLIENT> clients{};
    struct sockaddr_in serverAddr {};
    fd_set readfds, writefds, exceptfds;
};

class SocketClient{
public:
    explicit SocketClient(const std::string&, uint16_t);
    ~SocketClient();

    //void connect();
    void disconnect();
    bool isConnected() const noexcept;
    void send(const std::string&);
    std::string receive(std::chrono::milliseconds timeout = std::chrono::seconds(5));

    void login(const std::string &username, const std::string &password);

private:
    static constexpr int BUFFERSIZE = 256;
    int clientFD = -1;
    bool connected = false;
    std::string host;
    uint16_t port;
    sockaddr_in clientAddr{};

    void setNonBlocking(bool enable);
    void setTimeouts(std::chrono::milliseconds timeout);
};
// ====================================================================