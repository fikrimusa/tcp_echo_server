#pragma once
#include <iostream>
#pragma pack(push, 1)

struct MessageHeader{
    uint16_t msgSize;
    uint8_t  msgType;  // 0=LoginReq, 1=LoginResp, 2=ChatMsg
    uint8_t  reqId;
};

struct LoginRequest{
    MessageHeader header;
    char username[32];
    char password[65];  // SHA256 hex (64 chars + null)
};

struct LoginResponse{
    MessageHeader header;
    uint16_t status;   // 0=FAILED, 1=OK
};

struct ChatMessage{
    MessageHeader header;
    char username[32];
    char text[256];
};

#pragma pack(pop)
