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

    _tofuPath = std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") + "/.securemsg_tofu.json";
    loadOrGenerateKeys();
    loadTofu();
}

// ---------------------------------------------------------------------------
// Key persistence
// ---------------------------------------------------------------------------
std::string Client::keysPath() const {
    return std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") + "/.securemsg_keys.bin";
}

void Client::loadOrGenerateKeys() {
    std::ifstream f(keysPath(), std::ios::binary);
    if (f) {
        f.read(reinterpret_cast<char*>(_pk), crypto_box_PUBLICKEYBYTES);
        f.read(reinterpret_cast<char*>(_sk), crypto_box_SECRETKEYBYTES);
        if (f.gcount() == crypto_box_SECRETKEYBYTES)
            return;
    }
    crypto_box_keypair(_pk, _sk);
    std::ofstream out(keysPath(), std::ios::binary);
    out.write(reinterpret_cast<char*>(_pk), crypto_box_PUBLICKEYBYTES);
    out.write(reinterpret_cast<char*>(_sk), crypto_box_SECRETKEYBYTES);
}

std::string Client::publicKeyB64() const {
    return b64Encode(_pk, crypto_box_PUBLICKEYBYTES);
}

// ---------------------------------------------------------------------------
// TOFU
// ---------------------------------------------------------------------------
void Client::loadTofu() {
    std::ifstream f(_tofuPath);
    if (!f) return;
    try {
        json j; f >> j;
        for (auto& [k, v] : j.items())
            _tofu[k] = v.get<std::string>();
    } catch (...) {}
}

void Client::saveTofu() {
    json j = _tofu;
    std::ofstream f(_tofuPath);
    f << j.dump(2);
}

bool Client::verifyOrPin(const std::string& user, const std::string& keyB64) {
    auto it = _tofu.find(user);
    if (it == _tofu.end()) {
        _tofu[user] = keyB64;
        saveTofu();
        std::cout << "[TOFU] Pinned public key for " << user << "\n";
        return true;
    }
    if (it->second != keyB64) {
        std::cerr << "[TOFU] WARNING: Public key for " << user
                  << " has changed! Possible MITM. Aborting.\n";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------
std::string Client::request(const std::string& method, const std::string& path,
                             const std::string& body) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

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
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        throw std::runtime_error(std::string("curl: ") + curl_easy_strerror(res));

    return response;
}

std::string Client::httpGet(const std::string& p)                          { return request("GET",    p, ""); }
std::string Client::httpPost(const std::string& p, const std::string& b)  { return request("POST",   p, b);  }
std::string Client::httpPut(const std::string& p, const std::string& b)   { return request("PUT",    p, b);  }
std::string Client::httpDelete(const std::string& p)                       { return request("DELETE", p, ""); }

// ---------------------------------------------------------------------------
// Crypto: encrypt plaintext for recipient using ephemeral X25519 + XSalsa20-Poly1305
// Ephemeral sender key provides forward secrecy; recipient decrypts with their sk.
// ---------------------------------------------------------------------------
std::string Client::encryptFor(const std::string& plaintext, const std::string& recipientPkB64) {
    auto recipientPkBytes = b64Decode(recipientPkB64);
    if (recipientPkBytes.size() != crypto_box_PUBLICKEYBYTES)
        throw std::runtime_error("Invalid recipient public key length");

    // Generate ephemeral keypair
    unsigned char epk[crypto_box_PUBLICKEYBYTES];
    unsigned char esk[crypto_box_SECRETKEYBYTES];
    crypto_box_keypair(epk, esk);

    // Random nonce
    unsigned char nonce[crypto_box_NONCEBYTES];
    randombytes_buf(nonce, sizeof(nonce));

    // Encrypt
    std::vector<unsigned char> ct(crypto_box_MACBYTES + plaintext.size());
    if (crypto_box_easy(ct.data(),
                        reinterpret_cast<const unsigned char*>(plaintext.data()),
                        plaintext.size(),
                        nonce,
                        recipientPkBytes.data(),
                        esk) != 0)
        throw std::runtime_error("Encryption failed");

    // Pack as JSON fields
    json j;
    j["ciphertext"]       = b64Encode(ct.data(), ct.size());
    j["nonce"]            = b64Encode(nonce, sizeof(nonce));
    j["ephemeralPublicKey"] = b64Encode(epk, sizeof(epk));
    return j.dump();
}

std::string Client::decryptMessage(const Message& msg) const {
    auto ct   = b64Decode(msg.ciphertext);
    auto nonc = b64Decode(msg.nonce);
    auto epk  = b64Decode(msg.ephemeralPublicKey);

    if (epk.size()  != crypto_box_PUBLICKEYBYTES) throw std::runtime_error("Bad ephemeral key");
    if (nonc.size() != crypto_box_NONCEBYTES)     throw std::runtime_error("Bad nonce");
    if (ct.size()   <  crypto_box_MACBYTES)       throw std::runtime_error("Ciphertext too short");

    std::vector<unsigned char> plain(ct.size() - crypto_box_MACBYTES);
    if (crypto_box_open_easy(plain.data(), ct.data(), ct.size(),
                             nonc.data(), epk.data(), _sk) != 0)
        throw std::runtime_error("Decryption failed — not addressed to you or tampered");

    return std::string(plain.begin(), plain.end());
}

// ---------------------------------------------------------------------------
// Auth
// ---------------------------------------------------------------------------
bool Client::signUp(const std::string& username, const std::string& password) {
    json body;
    body["username"]  = username;
    body["password"]  = password;
    body["publicKey"] = publicKeyB64();

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
        return User{ j["id"], j["username"], j["publicKey"] };
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

std::optional<Conversation> Client::createConversation(const std::vector<std::string>& usernames) {
    json body;
    body["participantUsernames"] = usernames;
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
    m.id                 = j.at("id");
    m.conversationId     = j.at("conversationId");
    m.senderUsername     = j.value("senderUsername", "[deleted]");
    m.ciphertext         = j.at("ciphertext");
    m.nonce              = j.at("nonce");
    m.ephemeralPublicKey = j.at("ephemeralPublicKey");
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
        body["conversationId"]   = conversationId;
        body["ciphertext"]       = encrypted["ciphertext"];
        body["nonce"]            = encrypted["nonce"];
        body["ephemeralPublicKey"] = encrypted["ephemeralPublicKey"];
        httpPost("/api/message", body.dump());
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Send failed: " << e.what() << "\n";
        return false;
    }
}

bool Client::forwardMessage(const Message& msg, const std::string& targetConversationId,
                             const std::string& recipientUsername) {
    // TOFU: verify recipient's public key before encrypting for them
    auto user = getUser(recipientUsername);
    if (!user) {
        std::cerr << "User not found: " << recipientUsername << "\n";
        return false;
    }
    if (!verifyOrPin(recipientUsername, user->publicKey))
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
