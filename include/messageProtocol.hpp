#pragma once
#include <iostream>
#pragma pack(push, 1)  // Ensure no padding in structs

struct MessageHeader{
    uint16_t msgSize; 
    uint8_t msgType;   // 0=LoginReq, 1=LoginResp, 2=EchoReq
    uint8_t reqId;
};

struct LoginRequest{
    MessageHeader header;
    char username[32];
    char password[32];
};

struct LoginResponse{
    MessageHeader header;
    uint16_t status;    // 0=FAILED, 1=OK
};

struct EchoRequest{
    MessageHeader header;
    uint16_t msgSize;
    char ciphertext[];
};

struct EchoResponse{
    MessageHeader header;
    uint16_t msgSize;
    char message[];
};

#pragma pack(pop)  // Restore default packing