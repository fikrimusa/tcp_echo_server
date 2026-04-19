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
#include <iomanip>
#include <atomic>
#include <thread>
#include <mutex>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <openssl/evp.h>
#include <sstream>
#include "messageProtocol.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

inline std::string sha256(const std::string& str) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if(!ctx) throw std::runtime_error("Failed to create EVP context");

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;

    if(EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
       EVP_DigestUpdate(ctx, str.c_str(), str.size()) != 1 ||
       EVP_DigestFinal_ex(ctx, hash, &hashLen) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SHA256 calculation failed");
    }
    EVP_MD_CTX_free(ctx);

    std::stringstream ss;
    for(unsigned int i = 0; i < hashLen; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    return ss.str();
}

// =================== SocketServer Class =============================
class SocketServer{
public:
    explicit SocketServer(uint16_t port);
    ~SocketServer();

    void run();
    void handleClientMessage(int clientFD, int slot);
    bool handleLoginRequest(int clientFD, const MessageHeader&, std::string& outUsername);
    void handleLoginResponse(int clientFD, bool validStatus, const MessageHeader&);
    void handleChatMessage(int clientFD, int slot, const MessageHeader&);
    void broadcastMessage(int senderSlot, const ChatMessage& msg);
    void handleClientDisconnect(int clientFD);
    uint32_t crc32(const std::string&);
    static constexpr auto generateCRCTable();
    void startConsoleListener();

    uint8_t generateUniqueReqID() {
        return globalReqID.fetch_add(1, std::memory_order_relaxed);
    }

private:
    static constexpr int MAXCLIENT = 3;
    int serverFD{-1};
    int wakeupReadFD{-1};
    int wakeupWriteFD{-1};
    std::array<int, MAXCLIENT> clients{};
    std::array<std::string, MAXCLIENT> usernames{};  // empty = not logged in
    struct sockaddr_in serverAddr{};
    std::thread consoleThread;
    std::atomic<bool> running{false};
    std::atomic<uint32_t> globalReqID{0};
};
// ====================================================================

// =================== SocketClient Class =============================
class SocketClient{
public:
    explicit SocketClient(const std::string& host, uint16_t port);
    ~SocketClient();

    void disconnect();
    void handleLoginRequest(const std::string& username, const std::string& password);
    bool handleLoginResponse();
    void sendChatMessage(const std::string& text);
    void startReceiveLoop();
    uint8_t getCurrentReqID() const { return currentReqID; }

private:
    int clientFD{-1};
    bool connected{false};
    std::string host;
    uint16_t port;
    sockaddr_in clientAddr{};
    uint8_t currentReqID{0};
    std::string username;
    std::thread recvThread;
    std::atomic<bool> running{false};
    std::mutex printMtx;
};
// ====================================================================
