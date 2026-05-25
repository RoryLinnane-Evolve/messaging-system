#pragma once
#include <array>
#include <string>
#include <sodium.h>

// Manages the user's long-term X25519 encryption keypair and Ed25519 signing keypair.
//
// Both secret keys are encrypted together at rest:
//   Argon2id (crypto_pwhash) derives a 32-byte wrapping key from the user's
//   passphrase. crypto_secretbox_easy then authenticates and encrypts the
//   concatenated secret keys. Both public keys are stored in plaintext.
//
// On-disk format (232 bytes):
//   [0..31]    Argon2id salt                    (32 bytes)
//   [32..55]   secretbox nonce                  (24 bytes)
//   [56..167]  encrypted secret keys + MAC      (112 bytes = 32 X25519 sk
//                                                + 64 Ed25519 sk + 16 MAC)
//   [168..199] X25519 encryption public key     (32 bytes)
//   [200..231] Ed25519 signing public key       (32 bytes)
class KeyStore {
public:
    using PK     = std::array<unsigned char, crypto_box_PUBLICKEYBYTES>;   // 32
    using SK     = std::array<unsigned char, crypto_box_SECRETKEYBYTES>;   // 32
    using SignPK = std::array<unsigned char, crypto_sign_PUBLICKEYBYTES>;  // 32
    using SignSK = std::array<unsigned char, crypto_sign_SECRETKEYBYTES>;  // 64

    // Load an existing key file from `path`, prompting for the passphrase.
    // If the file does not exist, generates new keypairs and saves them.
    // Throws std::runtime_error on failure.
    [[nodiscard]] static KeyStore load(const std::string& path);

    const PK&     publicKey()        const noexcept { return _pk; }
    const SK&     secretKey()        const noexcept { return _sk; }
    const SignPK& signingPublicKey() const noexcept { return _signPk; }
    const SignSK& signingSecretKey() const noexcept { return _signSk; }

private:
    KeyStore(PK pk, SK sk, SignPK signPk, SignSK signSk)
        : _pk(pk), _sk(sk), _signPk(signPk), _signSk(signSk) {}

    static KeyStore    generate(const std::string& path);
    static KeyStore    loadExisting(const std::string& path);
    static void        persist(const std::string& path,
                               const PK& pk, const SK& sk,
                               const SignPK& signPk, const SignSK& signSk);
    static std::string readPassphrase(const std::string& label);

    PK     _pk;
    SK     _sk;
    SignPK _signPk;
    SignSK _signSk;
};
