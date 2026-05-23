#include "KeyStore.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <termios.h>
#include <unistd.h>

// On-disk sizes
static constexpr size_t SALT_LEN      = 32;
static constexpr size_t NONCE_LEN     = crypto_secretbox_NONCEBYTES;       // 24
static constexpr size_t ENC_SK_LEN    = crypto_box_SECRETKEYBYTES
                                      + crypto_secretbox_MACBYTES;          // 48
static constexpr size_t FILE_SIZE     = SALT_LEN + NONCE_LEN
                                      + ENC_SK_LEN + crypto_box_PUBLICKEYBYTES; // 136

// ---------------------------------------------------------------------------
// Read a passphrase from the terminal without echoing it
// ---------------------------------------------------------------------------
std::string KeyStore::readPassphrase(const std::string& label) {
    std::cout << label << std::flush;

    struct termios old{}, nw{};
    tcgetattr(STDIN_FILENO, &old);
    nw = old;
    nw.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &nw);

    std::string pass;
    std::getline(std::cin, pass);

    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    std::cout << "\n";
    return pass;
}

// ---------------------------------------------------------------------------
// Derive a wrapping key from a passphrase using Argon2id
// ---------------------------------------------------------------------------
static std::array<unsigned char, 32> deriveKey(const std::string& passphrase,
                                                const unsigned char* salt) {
    std::array<unsigned char, 32> key{};
    if (crypto_pwhash(key.data(), key.size(),
                      passphrase.c_str(), passphrase.size(),
                      salt,
                      crypto_pwhash_OPSLIMIT_INTERACTIVE,
                      crypto_pwhash_MEMLIMIT_INTERACTIVE,
                      crypto_pwhash_ALG_ARGON2ID13) != 0)
        throw std::runtime_error("Key derivation failed (out of memory?)");
    return key;
}

// ---------------------------------------------------------------------------
// Save encrypted keypair to disk
// ---------------------------------------------------------------------------
void KeyStore::persist(const std::string& path, const PK& pk, const SK& sk) {
    const auto pass = readPassphrase("Choose a passphrase to protect your key: ");
    if (pass.empty())
        throw std::runtime_error("Passphrase must not be empty");

    // Generate salt and nonce
    std::array<unsigned char, SALT_LEN>  salt{};
    std::array<unsigned char, NONCE_LEN> nonce{};
    randombytes_buf(salt.data(),  salt.size());
    randombytes_buf(nonce.data(), nonce.size());

    const auto wrapKey = deriveKey(pass, salt.data());

    // Encrypt the secret key
    std::array<unsigned char, ENC_SK_LEN> encSk{};
    crypto_secretbox_easy(encSk.data(), sk.data(), sk.size(),
                          nonce.data(), wrapKey.data());

    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("Cannot write key file: " + path);

    f.write(reinterpret_cast<const char*>(salt.data()),  SALT_LEN);
    f.write(reinterpret_cast<const char*>(nonce.data()), NONCE_LEN);
    f.write(reinterpret_cast<const char*>(encSk.data()), ENC_SK_LEN);
    f.write(reinterpret_cast<const char*>(pk.data()),    pk.size());
}

// ---------------------------------------------------------------------------
// Load and decrypt keypair from disk
// ---------------------------------------------------------------------------
KeyStore KeyStore::loadExisting(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Key file not found: " + path);

    std::array<unsigned char, SALT_LEN>  salt{};
    std::array<unsigned char, NONCE_LEN> nonce{};
    std::array<unsigned char, ENC_SK_LEN> encSk{};
    PK pk{};

    f.read(reinterpret_cast<char*>(salt.data()),  SALT_LEN);
    f.read(reinterpret_cast<char*>(nonce.data()), NONCE_LEN);
    f.read(reinterpret_cast<char*>(encSk.data()), ENC_SK_LEN);
    f.read(reinterpret_cast<char*>(pk.data()),    pk.size());

    if (!f) throw std::runtime_error("Key file is corrupt or truncated");

    const auto pass = readPassphrase("Key passphrase: ");
    const auto wrapKey = deriveKey(pass, salt.data());

    SK sk{};
    if (crypto_secretbox_open_easy(sk.data(), encSk.data(), encSk.size(),
                                   nonce.data(), wrapKey.data()) != 0)
        throw std::runtime_error("Wrong passphrase or corrupt key file");

    return KeyStore{pk, sk};
}

// ---------------------------------------------------------------------------
// Generate a fresh keypair, encrypt, and persist
// ---------------------------------------------------------------------------
KeyStore KeyStore::generate(const std::string& path) {
    std::cout << "No key file found — generating a new keypair.\n";
    PK pk{};
    SK sk{};
    crypto_box_keypair(pk.data(), sk.data());
    persist(path, pk, sk);
    std::cout << "Keypair saved to " << path << "\n";
    return KeyStore{pk, sk};
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------
KeyStore KeyStore::load(const std::string& path) {
    std::ifstream probe(path, std::ios::binary);
    if (!probe) return generate(path);
    probe.close();
    return loadExisting(path);
}
