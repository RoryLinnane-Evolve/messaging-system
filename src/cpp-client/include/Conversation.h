#pragma once
#include <string>
#include <vector>
#include "Message.h"

struct Conversation {
    std::string id;
    std::vector<std::string> participants;
    std::vector<Message> messages;
    std::string createdAt;
};
