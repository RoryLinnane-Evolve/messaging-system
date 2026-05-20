#pragma once
#include <string>
#include <optional>
#include <vector>
#include <map>
#include <memory>
#include <sodium.h>
#include "User.h"
#include "Message.h"
#include "Conversation.h"
#include "MessageStore.h"

class Client {
public:
    explicit Client(std::string baseUrl);

    // Auth
    bool signUp(const std::string& username, const std::string& password);
    bool login(const std::string& username, const std::string& password);
    bool changePassword(const std::string& current, const std::string& newPass);
    bool deleteAccount();

    // User
    std::optional<User> getUser(const std::string& username);

    // Conversations
    std::vector<Conversation> getConversations();
    std::optional<Conversation> getConversation(const std::string& id);
    std::optional<Conversation> createConversation(const std::vector<std::string>& usernames);

    // Messages
    std::vector<Message> getMessages(const std::string& conversationId);
    bool sendMessage(const std::string& conversationId, const std::string& plaintext,
                     const std::string& recipientPublicKeyB64);
    // Forward a message to another conversation, with TOFU check on recipient
    bool forwardMessage(const Message& msg, const std::string& targetConversationId,
                        const std::string& recipientUsername);
    bool deleteMessage(const std::string& id);
    bool revokeAccess(const std::string& conversationId, const std::string& targetUserId);

    // Save decrypted message content to a local file
    bool downloadMessage(const Message& msg, const std::string& filepath);

    // Crypto: decrypt a message addressed to this client
    std::string decryptMessage(const Message& msg) const;

    bool isLoggedIn() const { return !_token.empty(); }
    const std::string& username() const { return _username; }
    MessageStore& store() { return *_store; }

    std::string publicKeyB64() const;

private:
    std::string _baseUrl;
    std::string _token;
    std::string _username;

    unsigned char _pk[crypto_box_PUBLICKEYBYTES];
    unsigned char _sk[crypto_box_SECRETKEYBYTES];

    // TOFU: username -> pinned base64 public key
    std::map<std::string, std::string> _tofu;
    std::string _tofuPath;

    std::unique_ptr<MessageStore> _store;

    // HTTP
    std::string httpGet(const std::string& path);
    std::string httpPost(const std::string& path, const std::string& body);
    std::string httpPut(const std::string& path, const std::string& body);
    std::string httpDelete(const std::string& path);
    std::string request(const std::string& method, const std::string& path,
                        const std::string& body);

    // Crypto
    std::string encryptFor(const std::string& plaintext, const std::string& recipientPkB64);

    // TOFU
    bool verifyOrPin(const std::string& username, const std::string& publicKeyB64);
    void loadTofu();
    void saveTofu();

    // Key persistence
    void loadOrGenerateKeys();
    std::string keysPath() const;
};
