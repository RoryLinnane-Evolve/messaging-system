#pragma once
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Conversation.h"
#include "KeyStore.h"
#include "Message.h"
#include "MessageStore.h"
#include "User.h"
#include "WebSocketClient.h"

class Client {
public:
    explicit Client(std::string baseUrl);

    // ── Auth ─────────────────────────────────────────────────────────────────
    [[nodiscard]] bool signUp(const std::string& username, const std::string& password);
    [[nodiscard]] bool login(const std::string& username, const std::string& password);
    [[nodiscard]] bool changePassword(const std::string& current, const std::string& newPass);
    [[nodiscard]] bool deleteAccount();

    // ── User ─────────────────────────────────────────────────────────────────
    [[nodiscard]] std::optional<User> getUser(const std::string& username);

    // ── Conversations ─────────────────────────────────────────────────────────
    [[nodiscard]] std::vector<Conversation> getConversations();
    [[nodiscard]] std::optional<Conversation> getConversation(const std::string& id);
    [[nodiscard]] std::optional<Conversation> createConversation(const std::string& recipientUsername);

    // ── Messages ──────────────────────────────────────────────────────────────
    [[nodiscard]] std::vector<Message> getMessages(const std::string& conversationId);
    [[nodiscard]] bool sendMessage(const std::string& conversationId,
                                   const std::string& plaintext,
                                   const std::string& recipientPublicKeyB64);
    [[nodiscard]] bool forwardMessage(const Message& msg,
                                      const std::string& targetConversationId,
                                      const std::string& recipientUsername);
    [[nodiscard]] bool deleteMessage(const std::string& id);
    [[nodiscard]] bool revokeAccess(const std::string& conversationId,
                                    const std::string& targetUserId);
    [[nodiscard]] bool downloadMessage(const Message& msg, const std::string& filepath);

    // ── Crypto ────────────────────────────────────────────────────────────────
    [[nodiscard]] std::string decryptMessage(const Message& msg) const;
    [[nodiscard]] std::string publicKeyB64() const;
    [[nodiscard]] std::string signingPublicKeyB64() const;

    // ── WebSocket ─────────────────────────────────────────────────────────────
    // Connect the real-time channel after login.
    void connectWebSocket();

    // Poll for an incoming real-time message; returns "" if none queued.
    [[nodiscard]] std::string pollWebSocket();

    [[nodiscard]] bool wsConnected() const { return _ws.isConnected(); }

    // ── TOFU (public so UI code can pin before entering a chat) ───────────────
    [[nodiscard]] bool verifyOrPin(const std::string& user,
                                   const std::string& encKeyB64,
                                   const std::string& signKeyB64);

    // ── Accessors ─────────────────────────────────────────────────────────────
    [[nodiscard]] bool isLoggedIn() const { return !_token.empty(); }
    [[nodiscard]] const std::string& username() const { return _username; }
    [[nodiscard]] MessageStore& store() { return *_store; }

private:
    std::string _baseUrl;
    std::string _token;
    std::string _username;

    std::unique_ptr<KeyStore>        _keys;
    std::unique_ptr<MessageStore>    _store;
    WebSocketClient                  _ws;

    // TOFU: username → { "enc": base64_enc_pk, "sign": base64_sign_pk }
    std::map<std::string, std::map<std::string, std::string>> _tofu;
    std::string                                                _tofuPath;

    // ── HTTP ──────────────────────────────────────────────────────────────────
    [[nodiscard]] std::string httpGet(const std::string& path);
    [[nodiscard]] std::string httpPost(const std::string& path, const std::string& body);
    [[nodiscard]] std::string httpPut(const std::string& path, const std::string& body);
    [[nodiscard]] std::string httpDelete(const std::string& path);
    [[nodiscard]] std::string request(const std::string& method,
                                      const std::string& path,
                                      const std::string& body);

    // ── Crypto helpers ────────────────────────────────────────────────────────
    [[nodiscard]] std::string encryptFor(const std::string& plaintext,
                                         const std::string& recipientPkB64);

    // ── TOFU ──────────────────────────────────────────────────────────────────
    void loadTofu();
    void saveTofu();
};
