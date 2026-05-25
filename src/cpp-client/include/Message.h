#pragma once
#include <string>

struct Message {
    std::string id;
    std::string conversationId;
    std::string senderUsername;
    std::string senderSigningKey;   // base64 Ed25519 signing public key (from server)
    std::string ciphertext;         // base64 ChaCha20-Poly1305 ciphertext + 16-byte tag
    std::string nonce;              // base64 12-byte CSPRNG nonce
    std::string ephemeralPublicKey; // base64 ephemeral X25519 public key
    std::string signature;          // base64 Ed25519 signature over ct||nonce||epk
    std::string timestamp;
};
