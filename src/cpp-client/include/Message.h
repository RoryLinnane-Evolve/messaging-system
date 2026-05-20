#pragma once
#include <string>

struct Message {
    std::string id;
    std::string conversationId;
    std::string senderUsername;
    std::string ciphertext;       // base64
    std::string nonce;            // base64
    std::string ephemeralPublicKey; // base64
    std::string timestamp;
};
