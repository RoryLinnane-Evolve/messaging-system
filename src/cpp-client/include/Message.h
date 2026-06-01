#pragma once
#include <string>

struct Message {
    std::string id;
    std::string conversationId;
    std::string senderUsername;
    std::string ciphertext;       // base64
    std::string nonce;            // base64
    std::string ephemeralPublicKey; // base64
    std::string signature;          // base64 Ed25519 detached signature (may be empty)
    std::string timestamp;
};
