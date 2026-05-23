#pragma once
#include <array>
#include <string>
#include <sodium.h>

// Manages the user's long-term X25519 keypair.
//
// The secret key is encrypted at rest:
//   Argon2id (crypto_pwhash) derives a 32-byte wrapping key from the user's
//   passphrase. crypto_secretbox_easy then authenticates and encrypts the
//   secret key. The public key is stored in plaintext alongside it.
//
// On-disk format (136 bytes):
//   [0..31]   Argon2id salt          (32 bytes)
//   [32..55]  secretbox nonce        (24 bytes)
//   [56..103] encrypted secret key   (48 bytes = 32 key + 16 MAC)
//   [104..135] public key            (32 bytes)
class KeyStore {
public:
    using PK = std::array<unsigned char, crypto_box_PUBLICKEYBYTES>;
    using SK = std::array<unsigned char, crypto_box_SECRETKEYBYTES>;

    // Load an existing key file from `path`, prompting for the passphrase.
    // If the file does not exist, generates a new keypair and saves it.
    // Throws std::runtime_error on failure.
    [[nodiscard]] static KeyStore load(const std::string& path);

    const PK& publicKey() const noexcept { return _pk; }
    const SK& secretKey() const noexcept { return _sk; }

private:
    KeyStore(PK pk, SK sk) : _pk(pk), _sk(sk) {}

    static KeyStore generate(const std::string& path);
    static KeyStore loadExisting(const std::string& path);
    static void     persist(const std::string& path, const PK& pk, const SK& sk);
    static std::string readPassphrase(const std::string& label);

    PK _pk;
    SK _sk;
};
