#pragma once
#include <string>

struct User {
    std::string id;
    std::string username;
    std::string publicKey; // base64-encoded X25519 public key
};
