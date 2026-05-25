#include "Client.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <sodium.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <cstdlib>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// HKDF-SHA256 (RFC 5869) using libsodium's HMAC-SHA256 primitive.
// hkdfExtract: PRK = HMAC-SHA256(salt, IKM)
// hkdfExpand:  OKM = HMAC-SHA256(PRK, info || 0x01)  [single block, L ≤ 32]
// ---------------------------------------------------------------------------
static void hkdfExtract(unsigned char prk[crypto_auth_hmacsha256_BYTES],
                         const unsigned char* salt, size_t saltLen,
                         const unsigned char* ikm,  size_t ikmLen) {
    crypto_auth_hmacsha256_state st;
    crypto_auth_hmacsha256_init(&st, salt, saltLen);
    crypto_auth_hmacsha256_update(&st, ikm, ikmLen);
    crypto_auth_hmacsha256_final(&st, prk);
}

static void hkdfExpand(unsigned char* okm, size_t L,
                        const unsigned char prk[crypto_auth_hmacsha256_BYTES],
                        const unsigned char* info, size_t infoLen) {
    if (L > crypto_auth_hmacsha256_BYTES)
        throw std::runtime_error("hkdfExpand: L must be ≤ 32 for single-block expand");
    const unsigned char counter = 0x01;
    unsigned char T1[crypto_auth_hmacsha256_BYTES];
    crypto_auth_hmacsha256_state st;
    crypto_auth_hmacsha256_init(&st, prk, crypto_auth_hmacsha256_BYTES);
    crypto_auth_hmacsha256_update(&st, info, infoLen);
    crypto_auth_hmacsha256_update(&st, &counter, 1);
    crypto_auth_hmacsha256_final(&st, T1);
    std::memcpy(okm, T1, L);
    sodium_memzero(T1, sizeof(T1));
}

// ---------------------------------------------------------------------------
// RAII CURL handle
// ---------------------------------------------------------------------------
struct CurlHandle {
    CURL* h;
    CurlHandle() : h(curl_easy_init()) {
        if (!h) throw std::runtime_error("curl_easy_init failed");
    }
    ~CurlHandle() { curl_easy_cleanup(h); }
    operator CURL*() const { return h; }
};

// ---------------------------------------------------------------------------
// CURL write callback
// ---------------------------------------------------------------------------
static size_t curlWrite(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

// ---------------------------------------------------------------------------
// Base64 helpers using libsodium
// ---------------------------------------------------------------------------
// URL-safe no-padding base64 (avoids +, /, = issues; cleaner to store in JSON/DB)
static std::string b64Encode(const unsigned char* data, size_t len) {
    size_t encodedLen = sodium_base64_encoded_len(len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    std::string out(encodedLen, '\0');
    sodium_bin2base64(out.data(), encodedLen, data, len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    // sodium_base64_encoded_len includes the null terminator — trim it
    while (!out.empty() && out.back() == '\0')
        out.pop_back();
    return out;
}

static std::vector<unsigned char> b64Decode(const std::string& raw) {
    // Strip whitespace and null bytes that might survive a JSON round-trip
    std::string s;
    s.reserve(raw.size());
    for (char c : raw)
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t' && c != '\0')
            s += c;

    if (s.empty())
        throw std::runtime_error("base64 decode failed: empty input");

    std::vector<unsigned char> out(s.size()); // upper bound
    size_t decoded = 0;
    if (sodium_base642bin(out.data(), out.size(),
                          s.c_str(), s.size(),
                          nullptr, &decoded, nullptr,
                          sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0) {
        throw std::runtime_error(
            "base64 decode failed (len=" + std::to_string(s.size()) +
            " val=\"" + s.substr(0, 32) + (s.size() > 32 ? "..." : "") + "\")");
    }
    out.resize(decoded);
    return out;
}

// ---------------------------------------------------------------------------
// Client constructor
// ---------------------------------------------------------------------------
Client::Client(std::string baseUrl)
    : _baseUrl(std::move(baseUrl)), _store(std::make_unique<MessageStore>())
{
    if (sodium_init() < 0)
        throw std::runtime_error("libsodium init failed");

    const char* home = std::getenv("HOME");
    std::string homeDir = home ? home : ".";
    _tofuPath = homeDir + "/.securemsg_tofu.json";
    _keys = std::make_unique<KeyStore>(KeyStore::load(homeDir + "/.securemsg_keys.bin"));
    loadTofu();
}

std::string Client::publicKeyB64() const {
    return b64Encode(_keys->publicKey().data(), crypto_box_PUBLICKEYBYTES);
}

std::string Client::signingPublicKeyB64() const {
    return b64Encode(_keys->signingPublicKey().data(), crypto_sign_PUBLICKEYBYTES);
}

// ---------------------------------------------------------------------------
// TOFU
// ---------------------------------------------------------------------------
void Client::loadTofu() {
    std::ifstream f(_tofuPath);
    if (!f) return;
    try {
        json j; f >> j;
        for (auto& [user, keys] : j.items())
            for (auto& [k, v] : keys.items())
                _tofu[user][k] = v.get<std::string>();
    } catch (...) {}
}

void Client::saveTofu() {
    json j = _tofu;
    std::ofstream f(_tofuPath);
    f << j.dump(2);
}

bool Client::verifyOrPin(const std::string& user,
                          const std::string& encKeyB64,
                          const std::string& signKeyB64) {
    auto it = _tofu.find(user);
    if (it == _tofu.end()) {
        _tofu[user]["enc"]  = encKeyB64;
        _tofu[user]["sign"] = signKeyB64;
        saveTofu();
        std::cout << "[TOFU] Pinned keys for " << user << "\n";
        return true;
    }
    bool ok = true;
    if (it->second["enc"] != encKeyB64) {
        std::cerr << "[TOFU] WARNING: Encryption key for " << user
                  << " has changed! Possible MITM.\n";
        ok = false;
    }
    if (it->second["sign"] != signKeyB64) {
        std::cerr << "[TOFU] WARNING: Signing key for " << user
                  << " has changed! Possible MITM.\n";
        ok = false;
    }
    return ok;
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------
std::string Client::request(const std::string& method, const std::string& path,
                             const std::string& body) {
    CurlHandle curl;
    std::string url = _baseUrl + path;
    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    // TLS: verify peer and host (never disable in production)
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!_token.empty()) {
        std::string auth = "Authorization: Bearer " + _token;
        headers = curl_slist_append(headers, auth.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    } else if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK)
        throw std::runtime_error(std::string("curl: ") + curl_easy_strerror(res));

    return response;
}

std::string Client::httpGet(const std::string& p)                          { return request("GET",    p, ""); }
std::string Client::httpPost(const std::string& p, const std::string& b)  { return request("POST",   p, b);  }
std::string Client::httpPut(const std::string& p, const std::string& b)   { return request("PUT",    p, b);  }
std::string Client::httpDelete(const std::string& p)                       { return request("DELETE", p, ""); }

// ---------------------------------------------------------------------------
// Crypto: authenticated encryption for a recipient
//
// Scheme:
//   1. Ephemeral X25519 keypair (esk, epk) — provides forward secrecy
//   2. X25519(esk, recipient_pk) → dh_out
//   3. HKDF-Extract(salt=epk, ikm=dh_out) → PRK        [RFC 5869 §2.2]
//   4. HKDF-Expand(PRK, "SecureMsg-v1-message-enc", 32) → enc_key  [§2.3]
//   5. ChaCha20-Poly1305-IETF encrypt(enc_key, nonce, plaintext) → ciphertext
//   6. Ed25519-Sign(sender_sk, ciphertext || nonce || epk) → signature
//
// The signature binds the sender's long-term identity to the ciphertext so
// the recipient can verify message origin (criterion 2b).
// ---------------------------------------------------------------------------
std::string Client::encryptFor(const std::string& plaintext,
                                const std::string& recipientPkB64) {
    auto recipientPk = b64Decode(recipientPkB64);
    if (recipientPk.size() != crypto_box_PUBLICKEYBYTES)
        throw std::runtime_error("Invalid recipient public key length");

    // 1. Ephemeral X25519 keypair
    unsigned char epk[crypto_box_PUBLICKEYBYTES];
    unsigned char esk[crypto_box_SECRETKEYBYTES];
    crypto_box_keypair(epk, esk);

    // 2. Raw X25519 DH
    unsigned char dh[crypto_scalarmult_BYTES];
    if (crypto_scalarmult(dh, esk, recipientPk.data()) != 0)
        throw std::runtime_error("X25519 DH failed (low-order point)");

    // 3+4. HKDF-SHA256: PRK then enc_key
    unsigned char prk[crypto_auth_hmacsha256_BYTES];
    hkdfExtract(prk, epk, sizeof(epk), dh, sizeof(dh));
    sodium_memzero(dh, sizeof(dh));

    unsigned char enc_key[crypto_aead_chacha20poly1305_ietf_KEYBYTES];
    static const char ENC_INFO[] = "SecureMsg-v1-message-enc";
    hkdfExpand(enc_key, sizeof(enc_key), prk,
               reinterpret_cast<const unsigned char*>(ENC_INFO), sizeof(ENC_INFO) - 1);
    sodium_memzero(prk, sizeof(prk));

    // 5. ChaCha20-Poly1305-IETF encrypt (12-byte nonce)
    unsigned char nonce[crypto_aead_chacha20poly1305_ietf_NPUBBYTES];
    randombytes_buf(nonce, sizeof(nonce));

    std::vector<unsigned char> ct(plaintext.size() + crypto_aead_chacha20poly1305_ietf_ABYTES);
    unsigned long long ct_len = 0;
    crypto_aead_chacha20poly1305_ietf_encrypt(
        ct.data(), &ct_len,
        reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
        nullptr, 0, nullptr, nonce, enc_key);
    sodium_memzero(enc_key, sizeof(enc_key));
    ct.resize(ct_len);

    // 6. Ed25519 sign: ciphertext || nonce || epk
    std::vector<unsigned char> sigMaterial;
    sigMaterial.insert(sigMaterial.end(), ct.begin(), ct.end());
    sigMaterial.insert(sigMaterial.end(), nonce, nonce + sizeof(nonce));
    sigMaterial.insert(sigMaterial.end(), epk,   epk   + sizeof(epk));

    unsigned char sig[crypto_sign_BYTES];
    if (crypto_sign_detached(sig, nullptr,
                             sigMaterial.data(), sigMaterial.size(),
                             _keys->signingSecretKey().data()) != 0)
        throw std::runtime_error("Ed25519 signing failed");

    json j;
    j["ciphertext"]         = b64Encode(ct.data(), ct.size());
    j["nonce"]              = b64Encode(nonce, sizeof(nonce));
    j["ephemeralPublicKey"] = b64Encode(epk, sizeof(epk));
    j["signature"]          = b64Encode(sig, sizeof(sig));
    return j.dump();
}

std::string Client::decryptMessage(const Message& msg) const {
    auto ct   = b64Decode(msg.ciphertext);
    auto nonc = b64Decode(msg.nonce);
    auto epk  = b64Decode(msg.ephemeralPublicKey);
    auto sig  = b64Decode(msg.signature);

    if (epk.size()  != crypto_box_PUBLICKEYBYTES)
        throw std::runtime_error("Bad ephemeral key length");
    if (nonc.size() != crypto_aead_chacha20poly1305_ietf_NPUBBYTES)
        throw std::runtime_error("Bad nonce length");
    if (ct.size()   <  crypto_aead_chacha20poly1305_ietf_ABYTES)
        throw std::runtime_error("Ciphertext too short");
    if (sig.size()  != crypto_sign_BYTES)
        throw std::runtime_error("Bad signature length");

    // Verify Ed25519 signature before doing any decryption work
    if (!msg.senderSigningKey.empty()) {
        auto signPk = b64Decode(msg.senderSigningKey);
        if (signPk.size() != crypto_sign_PUBLICKEYBYTES)
            throw std::runtime_error("Bad sender signing key length");

        std::vector<unsigned char> sigMaterial;
        sigMaterial.insert(sigMaterial.end(), ct.begin(),   ct.end());
        sigMaterial.insert(sigMaterial.end(), nonc.begin(), nonc.end());
        sigMaterial.insert(sigMaterial.end(), epk.begin(),  epk.end());

        if (crypto_sign_verify_detached(sig.data(),
                                        sigMaterial.data(), sigMaterial.size(),
                                        signPk.data()) != 0)
            throw std::runtime_error(
                "Signature verification FAILED for message from " +
                msg.senderUsername + " — message may be forged");
    }

    // X25519 DH: recipient_sk × ephemeral_pk
    unsigned char dh[crypto_scalarmult_BYTES];
    if (crypto_scalarmult(dh, _keys->secretKey().data(), epk.data()) != 0)
        throw std::runtime_error("X25519 DH failed");

    // HKDF-SHA256 → enc_key
    unsigned char prk[crypto_auth_hmacsha256_BYTES];
    hkdfExtract(prk, epk.data(), epk.size(), dh, sizeof(dh));
    sodium_memzero(dh, sizeof(dh));

    unsigned char enc_key[crypto_aead_chacha20poly1305_ietf_KEYBYTES];
    static const char ENC_INFO[] = "SecureMsg-v1-message-enc";
    hkdfExpand(enc_key, sizeof(enc_key), prk,
               reinterpret_cast<const unsigned char*>(ENC_INFO), sizeof(ENC_INFO) - 1);
    sodium_memzero(prk, sizeof(prk));

    // ChaCha20-Poly1305-IETF decrypt
    std::vector<unsigned char> plain(ct.size() - crypto_aead_chacha20poly1305_ietf_ABYTES);
    unsigned long long plain_len = 0;
    if (crypto_aead_chacha20poly1305_ietf_decrypt(
            plain.data(), &plain_len, nullptr,
            ct.data(), ct.size(),
            nullptr, 0,
            nonc.data(), enc_key) != 0) {
        sodium_memzero(enc_key, sizeof(enc_key));
        throw std::runtime_error("ChaCha20-Poly1305 authentication/decryption failed");
    }
    sodium_memzero(enc_key, sizeof(enc_key));
    plain.resize(plain_len);
    return std::string(plain.begin(), plain.end());
}

// ---------------------------------------------------------------------------
// Auth
// ---------------------------------------------------------------------------
bool Client::signUp(const std::string& username, const std::string& password) {
    json body;
    body["username"]         = username;
    body["password"]         = password;
    body["publicKey"]        = publicKeyB64();
    body["signingPublicKey"] = signingPublicKeyB64();

    try {
        httpPost("/api/auth/sign-up", body.dump());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Sign-up failed: " << e.what() << "\n";
        return false;
    }
}

bool Client::login(const std::string& username, const std::string& password) {
    json body;
    body["username"] = username;
    body["password"] = password;

    try {
        auto resp = httpPost("/api/auth/login", body.dump());
        auto j = json::parse(resp);
        _token    = j.at("token").get<std::string>();
        _username = username;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Login failed: " << e.what() << "\n";
        return false;
    }
}

bool Client::changePassword(const std::string& current, const std::string& newPass) {
    json body;
    body["currentPassword"] = current;
    body["newPassword"]     = newPass;
    try {
        httpPut("/api/user/password", body.dump());
        return true;
    } catch (...) { return false; }
}

bool Client::deleteAccount() {
    try {
        httpDelete("/api/user");
        _token.clear();
        _username.clear();
        return true;
    } catch (...) { return false; }
}

// ---------------------------------------------------------------------------
// User
// ---------------------------------------------------------------------------
std::optional<User> Client::getUser(const std::string& username) {
    try {
        auto resp = httpGet("/api/user/" + username);
        auto j = json::parse(resp);
        return User{
            j["id"].get<std::string>(),
            j["username"].get<std::string>(),
            j["publicKey"].get<std::string>(),
            j["signingPublicKey"].get<std::string>()
        };
    } catch (...) { return std::nullopt; }
}

// ---------------------------------------------------------------------------
// Conversations
// ---------------------------------------------------------------------------
static Conversation parseConversation(const json& j) {
    Conversation c;
    c.id        = j.at("id");
    c.createdAt = j.value("createdAt", "");
    for (auto& p : j.at("participants"))
        c.participants.push_back(p.get<std::string>());
    return c;
}

std::vector<Conversation> Client::getConversations() {
    try {
        auto resp = httpGet("/api/conversation");
        auto arr  = json::parse(resp);
        std::vector<Conversation> result;
        for (auto& item : arr)
            result.push_back(parseConversation(item));
        _store->setConversations(result);
        return result;
    } catch (...) { return {}; }
}

std::optional<Conversation> Client::getConversation(const std::string& id) {
    try {
        auto resp = httpGet("/api/conversation/" + id);
        return parseConversation(json::parse(resp));
    } catch (...) { return std::nullopt; }
}

std::optional<Conversation> Client::createConversation(const std::string& recipientUsername) {
    json body;
    body["recipientUsername"] = recipientUsername;
    try {
        auto resp = httpPost("/api/conversation", body.dump());
        return parseConversation(json::parse(resp));
    } catch (const std::exception& e) {
        std::cerr << "Create conversation failed: " << e.what() << "\n";
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------
static Message parseMessage(const json& j) {
    Message m;
    m.id                 = j.at("id").get<std::string>();
    m.conversationId     = j.at("conversationId").get<std::string>();
    m.senderUsername     = j.value("senderUsername", "[deleted]");
    m.senderSigningKey   = j.value("senderSigningKey", "");
    m.ciphertext         = j.at("ciphertext").get<std::string>();
    m.nonce              = j.at("nonce").get<std::string>();
    m.ephemeralPublicKey = j.at("ephemeralPublicKey").get<std::string>();
    m.signature          = j.value("signature", "");
    m.timestamp          = j.value("timestamp", "");
    return m;
}

std::vector<Message> Client::getMessages(const std::string& conversationId) {
    try {
        auto resp = httpGet("/api/message/conversation/" + conversationId);
        auto arr  = json::parse(resp);
        std::vector<Message> msgs;
        for (auto& item : arr)
            msgs.push_back(parseMessage(item));
        _store->setMessages(conversationId, msgs);
        return msgs;
    } catch (...) { return {}; }
}

bool Client::sendMessage(const std::string& conversationId,
                         const std::string& plaintext,
                         const std::string& recipientPublicKeyB64) {
    try {
        auto encrypted = json::parse(encryptFor(plaintext, recipientPublicKeyB64));
        json body;
        body["conversationId"]    = conversationId;
        body["ciphertext"]        = encrypted["ciphertext"];
        body["nonce"]             = encrypted["nonce"];
        body["ephemeralPublicKey"] = encrypted["ephemeralPublicKey"];
        body["signature"]         = encrypted["signature"];
        httpPost("/api/message", body.dump());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Send failed: " << e.what() << "\n";
        return false;
    }
}

bool Client::forwardMessage(const Message& msg, const std::string& targetConversationId,
                             const std::string& recipientUsername) {
    // TOFU: verify both keys for recipient before encrypting
    auto user = getUser(recipientUsername);
    if (!user) {
        std::cerr << "User not found: " << recipientUsername << "\n";
        return false;
    }
    if (!verifyOrPin(recipientUsername, user->publicKey, user->signingPublicKey))
        return false; // TOFU mismatch — abort

    // Decrypt original, re-encrypt for recipient
    std::string plaintext;
    try {
        plaintext = decryptMessage(msg);
    } catch (const std::exception& e) {
        std::cerr << "Could not decrypt original message: " << e.what() << "\n";
        return false;
    }

    return sendMessage(targetConversationId, plaintext, user->publicKey);
}

bool Client::deleteMessage(const std::string& id) {
    try {
        httpDelete("/api/message/" + id);
        return true;
    } catch (...) { return false; }
}

bool Client::revokeAccess(const std::string& conversationId, const std::string& targetUserId) {
    try {
        httpPost("/api/message/" + conversationId + "/revoke/" + targetUserId, "{}");
        return true;
    } catch (...) { return false; }
}

bool Client::downloadMessage(const Message& msg, const std::string& filepath) {
    try {
        auto plaintext = decryptMessage(msg);
        std::ofstream f(filepath);
        if (!f) return false;
        f << "From:      " << msg.senderUsername << "\n"
          << "Timestamp: " << msg.timestamp       << "\n"
          << "MessageID: " << msg.id              << "\n"
          << "---\n"
          << plaintext << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Download failed: " << e.what() << "\n";
        return false;
    }
}

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------
void Client::connectWebSocket() {
    // Derive WS URL from the HTTP base URL
    std::string wsUrl = _baseUrl;
    if (wsUrl.rfind("https://", 0) == 0)
        wsUrl.replace(0, 8, "wss://");
    else if (wsUrl.rfind("http://", 0) == 0)
        wsUrl.replace(0, 7, "ws://");
    wsUrl += "/ws?token=" + _token;
    _ws.connect(wsUrl);
}

std::string Client::pollWebSocket() {
    return _ws.poll();
}
