#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <memory>
#include "Message.h"
#include "Conversation.h"

// In-memory cache of conversations and messages fetched from the server
class MessageStore {
public:
    void setConversations(std::vector<Conversation> convs) {
        _conversations = std::move(convs);
    }

    void setMessages(const std::string& conversationId, std::vector<Message> msgs) {
        // Remove existing messages for this conversation
        _messages.erase(
            std::remove_if(_messages.begin(), _messages.end(),
                [&](const Message& m) { return m.conversationId == conversationId; }),
            _messages.end()
        );
        std::copy(msgs.begin(), msgs.end(), std::back_inserter(_messages));
    }

    void addMessage(Message msg) {
        _messages.push_back(std::move(msg));
    }

    const std::vector<Conversation>& conversations() const { return _conversations; }

    std::vector<Message> getMessages(const std::string& conversationId) const {
        std::vector<Message> result;
        std::copy_if(_messages.begin(), _messages.end(), std::back_inserter(result),
            [&](const Message& m) { return m.conversationId == conversationId; });

        std::sort(result.begin(), result.end(),
            [](const Message& a, const Message& b) { return a.timestamp < b.timestamp; });

        return result;
    }

    int countMessages(const std::string& conversationId) const {
        return static_cast<int>(std::count_if(_messages.begin(), _messages.end(),
            [&](const Message& m) { return m.conversationId == conversationId; }));
    }

    const Conversation* findConversation(const std::string& id) const {
        auto it = std::find_if(_conversations.begin(), _conversations.end(),
            [&](const Conversation& c) { return c.id == id; });
        return it != _conversations.end() ? &(*it) : nullptr;
    }

private:
    std::vector<Conversation> _conversations;
    std::vector<Message> _messages;
};
