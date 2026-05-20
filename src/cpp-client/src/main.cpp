#include "Client.h"
#include <iostream>
#include <sstream>
#include <string>

static std::string prompt(const std::string& label) {
    std::cout << label;
    std::string s;
    std::getline(std::cin, s);
    return s;
}

static void printSeparator() { std::cout << std::string(50, '-') << "\n"; }

// ---------------------------------------------------------------------------
// Sub-menus
// ---------------------------------------------------------------------------
static void menuMessages(Client& client) {
    auto convs = client.getConversations();
    if (convs.empty()) { std::cout << "No conversations.\n"; return; }

    std::cout << "Conversations:\n";
    for (size_t i = 0; i < convs.size(); ++i) {
        std::cout << "  [" << i << "] " << convs[i].id << " (";
        for (auto& p : convs[i].participants) std::cout << p << " ";
        std::cout << ")\n";
    }

    auto idxStr = prompt("Select conversation index: ");
    size_t idx;
    try { idx = std::stoul(idxStr); } catch (...) { return; }
    if (idx >= convs.size()) return;

    const auto& conv = convs[idx];
    auto msgs = client.getMessages(conv.id);

    if (msgs.empty()) { std::cout << "No messages.\n"; return; }

    printSeparator();
    for (size_t i = 0; i < msgs.size(); ++i) {
        const auto& m = msgs[i];
        std::string plaintext;
        try { plaintext = client.decryptMessage(m); }
        catch (...) { plaintext = "[could not decrypt]"; }

        std::cout << "[" << i << "] " << m.senderUsername
                  << " @ " << m.timestamp << "\n"
                  << "    " << plaintext << "\n";
    }
    printSeparator();

    std::cout << "  [d] Delete a message\n"
              << "  [f] Forward a message\n"
              << "  [s] Save a message to file\n"
              << "  [b] Back\n";

    auto choice = prompt("> ");
    if (choice == "d") {
        auto iStr = prompt("Message index: ");
        size_t mi;
        try { mi = std::stoul(iStr); } catch (...) { return; }
        if (mi >= msgs.size()) return;
        if (client.deleteMessage(msgs[mi].id))
            std::cout << "Deleted.\n";
    } else if (choice == "f") {
        auto iStr = prompt("Message index: ");
        size_t mi;
        try { mi = std::stoul(iStr); } catch (...) { return; }
        if (mi >= msgs.size()) return;

        auto targetConvId  = prompt("Target conversation ID: ");
        auto recipientUser = prompt("Recipient username (for TOFU check): ");
        if (client.forwardMessage(msgs[mi], targetConvId, recipientUser))
            std::cout << "Forwarded.\n";
    } else if (choice == "s") {
        auto iStr = prompt("Message index: ");
        size_t mi;
        try { mi = std::stoul(iStr); } catch (...) { return; }
        if (mi >= msgs.size()) return;
        auto path = prompt("Save to file path: ");
        if (client.downloadMessage(msgs[mi], path))
            std::cout << "Saved to " << path << "\n";
    }
}

static void menuSend(Client& client) {
    auto convs = client.getConversations();
    if (convs.empty()) { std::cout << "No conversations. Create one first.\n"; return; }

    std::cout << "Conversations:\n";
    for (size_t i = 0; i < convs.size(); ++i) {
        std::cout << "  [" << i << "] ";
        for (auto& p : convs[i].participants) std::cout << p << " ";
        std::cout << "\n";
    }

    auto idxStr = prompt("Select conversation index: ");
    size_t idx;
    try { idx = std::stoul(idxStr); } catch (...) { return; }
    if (idx >= convs.size()) return;

    const auto& conv = convs[idx];

    // Find a participant who is not us to get their public key
    std::string recipientUsername;
    for (auto& p : conv.participants) {
        if (p != client.username()) { recipientUsername = p; break; }
    }
    if (recipientUsername.empty()) { std::cout << "No other participant found.\n"; return; }

    auto user = client.getUser(recipientUsername);
    if (!user) { std::cout << "Could not fetch recipient.\n"; return; }

    auto text = prompt("Message: ");
    if (client.sendMessage(conv.id, text, user->publicKey))
        std::cout << "Sent.\n";
}

static void menuCreateConversation(Client& client) {
    auto names = prompt("Participant usernames (space-separated, not including yourself): ");
    std::vector<std::string> participants;
    std::istringstream iss(names);
    std::string token;
    while (iss >> token) participants.push_back(token);

    if (participants.empty()) return;
    auto conv = client.createConversation(participants);
    if (conv) std::cout << "Created conversation: " << conv->id << "\n";
    else std::cout << "Failed to create conversation.\n";
}

static void menuRevokeAccess(Client& client) {
    auto convId  = prompt("Conversation ID: ");
    auto username = prompt("Username to revoke: ");
    auto user = client.getUser(username);
    if (!user) { std::cout << "User not found.\n"; return; }
    if (client.revokeAccess(convId, user->id))
        std::cout << "Access revoked.\n";
}

static void menuAccount(Client& client) {
    std::cout << "  [1] Change password\n"
              << "  [2] Delete account\n"
              << "  [b] Back\n";
    auto c = prompt("> ");
    if (c == "1") {
        auto cur = prompt("Current password: ");
        auto nw  = prompt("New password: ");
        if (client.changePassword(cur, nw))
            std::cout << "Password changed.\n";
        else
            std::cout << "Failed.\n";
    } else if (c == "2") {
        auto confirm = prompt("Type DELETE to confirm: ");
        if (confirm == "DELETE" && client.deleteAccount())
            std::cout << "Account deleted.\n";
    }
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
static void mainMenu(Client& client) {
    while (true) {
        printSeparator();
        std::cout << "Logged in as: " << client.username() << "\n"
                  << "  [1] View conversations & messages\n"
                  << "  [2] Send a message\n"
                  << "  [3] New conversation\n"
                  << "  [4] Revoke access\n"
                  << "  [5] Account settings\n"
                  << "  [q] Logout & quit\n";
        printSeparator();

        auto c = prompt("> ");
        if      (c == "1") menuMessages(client);
        else if (c == "2") menuSend(client);
        else if (c == "3") menuCreateConversation(client);
        else if (c == "4") menuRevokeAccess(client);
        else if (c == "5") menuAccount(client);
        else if (c == "q") break;
    }
}

int main(int argc, char* argv[]) {
    std::string baseUrl = "http://localhost:8080";
    if (argc > 1) baseUrl = argv[1];

    std::cout << "SecureMsg C++ Client\n"
              << "Server: " << baseUrl << "\n";
    printSeparator();

    Client client(baseUrl);

    while (true) {
        std::cout << "  [1] Login\n"
                  << "  [2] Sign up\n"
                  << "  [q] Quit\n";
        auto c = prompt("> ");

        if (c == "1") {
            auto user = prompt("Username: ");
            auto pass = prompt("Password: ");
            if (client.login(user, pass)) {
                std::cout << "Logged in.\n";
                mainMenu(client);
            } else {
                std::cout << "Invalid credentials.\n";
            }
        } else if (c == "2") {
            auto user = prompt("Username: ");
            auto pass = prompt("Password: ");
            if (client.signUp(user, pass)) {
                std::cout << "Registered. Logging in...\n";
                if (client.login(user, pass))
                    mainMenu(client);
            }
        } else if (c == "q") {
            break;
        }
    }
    return 0;
}
