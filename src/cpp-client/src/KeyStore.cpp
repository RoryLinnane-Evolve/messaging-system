#include "KeyStore.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <termios.h>
#include <unistd.h>

// On-disk sizes
static constexpr size_t SALT_LEN      = 32;
static constexpr size_t NONCE_LEN     = crypto_secretbox_NONCEBYTES;         // 24
// Plaintext = X25519 sk (32) + Ed25519 sk (64) = 96 bytes
static constexpr size_t PLAIN_SK_LEN  = crypto_box_SECRETKEYBYTES
                                      + crypto_sign_SECRETKEYBYTES;          // 96
static constexpr size_t ENC_SK_LEN    = PLAIN_SK_LEN
                                      + crypto_secretbox_MACBYTES;           // 112
static constexpr size_t FILE_SIZE     = SALT_LEN + NONCE_LEN + ENC_SK_LEN
                                      + crypto_box_PUBLICKEYBYTES
                                      + crypto_sign_PUBLICKEYBYTES;          // 232

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
// Save both keypairs encrypted to disk
// ---------------------------------------------------------------------------
void KeyStore::persist(const std::string& path,
                       const PK& pk, const SK& sk,
                       const SignPK& signPk, const SignSK& signSk) {
    const auto pass = readPassphrase("Choose a passphrase to protect your key: ");
    if (pass.empty())
        throw std::runtime_error("Passphrase must not be empty");

    std::array<unsigned char, SALT_LEN>  salt{};
    std::array<unsigned char, NONCE_LEN> nonce{};
    randombytes_buf(salt.data(),  salt.size());
    randombytes_buf(nonce.data(), nonce.size());

    const auto wrapKey = deriveKey(pass, salt.data());

    // Concatenate both secret keys into one plaintext block
    std::array<unsigned char, PLAIN_SK_LEN> plainSks{};
    std::copy(sk.begin(),     sk.end(),     plainSks.begin());
    std::copy(signSk.begin(), signSk.end(), plainSks.begin() + crypto_box_SECRETKEYBYTES);

    // Encrypt both secret keys together under the wrapping key
    std::array<unsigned char, ENC_SK_LEN> encSks{};
    crypto_secretbox_easy(encSks.data(), plainSks.data(), PLAIN_SK_LEN,
                          nonce.data(), wrapKey.data());
    sodium_memzero(plainSks.data(), PLAIN_SK_LEN);

    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("Cannot write key file: " + path);

    f.write(reinterpret_cast<const char*>(salt.data()),   SALT_LEN);
    f.write(reinterpret_cast<const char*>(nonce.data()),  NONCE_LEN);
    f.write(reinterpret_cast<const char*>(encSks.data()), ENC_SK_LEN);
    f.write(reinterpret_cast<const char*>(pk.data()),     pk.size());
    f.write(reinterpret_cast<const char*>(signPk.data()), signPk.size());
}

// ---------------------------------------------------------------------------
// Load and decrypt both keypairs from disk
// ---------------------------------------------------------------------------
KeyStore KeyStore::loadExisting(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Key file not found: " + path);

    std::array<unsigned char, SALT_LEN>   salt{};
    std::array<unsigned char, NONCE_LEN>  nonce{};
    std::array<unsigned char, ENC_SK_LEN> encSks{};
    PK     pk{};
    SignPK signPk{};

    f.read(reinterpret_cast<char*>(salt.data()),   SALT_LEN);
    f.read(reinterpret_cast<char*>(nonce.data()),  NONCE_LEN);
    f.read(reinterpret_cast<char*>(encSks.data()), ENC_SK_LEN);
    f.read(reinterpret_cast<char*>(pk.data()),     pk.size());
    f.read(reinterpret_cast<char*>(signPk.data()), signPk.size());

    if (!f) throw std::runtime_error("Key file is corrupt or truncated");

    const auto pass    = readPassphrase("Key passphrase: ");
    const auto wrapKey = deriveKey(pass, salt.data());

    std::array<unsigned char, PLAIN_SK_LEN> plainSks{};
    if (crypto_secretbox_open_easy(plainSks.data(), encSks.data(), ENC_SK_LEN,
                                   nonce.data(), wrapKey.data()) != 0)
        throw std::runtime_error("Wrong passphrase or corrupt key file");

    SK     sk{};
    SignSK signSk{};
    std::copy(plainSks.begin(),
              plainSks.begin() + crypto_box_SECRETKEYBYTES, sk.begin());
    std::copy(plainSks.begin() + crypto_box_SECRETKEYBYTES,
              plainSks.end(), signSk.begin());
    sodium_memzero(plainSks.data(), PLAIN_SK_LEN);

    return KeyStore{pk, sk, signPk, signSk};
}

// ---------------------------------------------------------------------------
// Generate fresh keypairs, encrypt, and persist
// ---------------------------------------------------------------------------
KeyStore KeyStore::generate(const std::string& path) {
    std::cout << "No key file found — generating new keypairs.\n";
    PK     pk{};
    SK     sk{};
    SignPK signPk{};
    SignSK signSk{};
    crypto_box_keypair(pk.data(), sk.data());
    crypto_sign_keypair(signPk.data(), signSk.data());
    persist(path, pk, sk, signPk, signSk);
    std::cout << "Keypairs saved to " << path << "\n";
    return KeyStore{pk, sk, signPk, signSk};
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
