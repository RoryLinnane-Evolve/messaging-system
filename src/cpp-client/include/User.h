#pragma once
#include <string>

struct User {
    std::string id;
    std::string username;
    std::string publicKey;        // base64-encoded X25519 encryption public key
    std::string signingPublicKey; // base64-encoded Ed25519 signing public key
};
