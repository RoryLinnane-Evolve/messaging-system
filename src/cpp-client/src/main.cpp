#include "Client.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <sys/select.h>
#include <ctime>
#include <unistd.h>

using json = nlohmann::json;

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

static void menuCreateConversation(Client& client) {
    auto recipient = prompt("Start conversation with (username): ");
    if (recipient.empty()) return;
    auto conv = client.createConversation(recipient);
    if (conv) std::cout << "Conversation ready: " << conv->id << "\n";
    else std::cout << "Failed to create conversation.\n";
}

// Trim an ISO timestamp to just HH:MM, e.g. "2026-05-23T14:05:32Z" → "14:05"
static std::string shortTime(const std::string& ts) {
    // Find the 'T' separator between date and time
    auto t = ts.find('T');
    if (t == std::string::npos || t + 6 > ts.size()) return ts;
    return ts.substr(t + 1, 5); // "HH:MM"
}

// ANSI helpers
static const char* BOLD  = "\033[1m";
static const char* DIM   = "\033[2m";
static const char* RESET = "\033[0m";
// Clear the current terminal line (wipes the input prompt before printing)
static const char* CLEAR_LINE = "\r\033[2K";

static void printMessage(const std::string& time, const std::string& sender,
                          const std::string& text, bool isSelf) {
    // Dim the timestamp, bold the sender name
    std::cout << DIM << time << RESET << "  "
              << BOLD << sender << RESET << ": "
              << text << "\n";
    (void)isSelf; // reserved for future colour support
}

// ---------------------------------------------------------------------------
// Chat — real-time via WebSocket, select() for non-blocking stdin
// ---------------------------------------------------------------------------
static void menuLiveChat(Client& client) {
    auto convs = client.getConversations();
    if (convs.empty()) { std::cout << "No conversations.\n"; return; }

    std::cout << "Your conversations:\n";
    for (size_t i = 0; i < convs.size(); ++i) {
        // Show the other participant's name, not our own
        std::string other;
        for (auto& p : convs[i].participants)
            if (p != client.username()) { other = p; break; }
        std::cout << "  [" << i << "] " << other << "\n";
    }

    auto idxStr = prompt("Select: ");
    size_t idx;
    try { idx = std::stoul(idxStr); } catch (...) { return; }
    if (idx >= convs.size()) return;

    const auto& conv = convs[idx];

    std::string recipientUsername;
    for (auto& p : conv.participants)
        if (p != client.username()) { recipientUsername = p; break; }
    if (recipientUsername.empty()) { std::cout << "No other participant.\n"; return; }

    auto recipientUser = client.getUser(recipientUsername);
    if (!recipientUser) { std::cout << "Could not fetch recipient info.\n"; return; }

    // Connect WebSocket for real-time delivery
    if (!client.wsConnected()) {
        client.connectWebSocket();
        struct timespec ts{0, 200'000'000};
        nanosleep(&ts, nullptr);
    }

    // Header
    std::cout << "\n" << BOLD << "  " << client.username()
              << "  ↔  " << recipientUsername << RESET << "\n";
    printSeparator();

    // Print history
    auto history = client.getMessages(conv.id);
    for (auto& m : history) {
        std::string text;
        try { text = client.decryptMessage(m); } catch (...) { text = "[encrypted]"; }
        printMessage(shortTime(m.timestamp), m.senderUsername, text,
                     m.senderUsername == client.username());
    }
    printSeparator();
    std::cout << DIM << "  /quit to exit\n" << RESET;

    const std::string promptStr = client.username() + "> ";
    std::cout << promptStr << std::flush;

    while (true) {
        // Drain all queued incoming WebSocket messages
        for (std::string raw; !(raw = client.pollWebSocket()).empty(); ) {
            try {
                auto j = nlohmann::json::parse(raw);
                if (j.value("type", "") == "new_message") {
                    auto& data = j.at("data");
                    Message m;
                    m.id                 = data.at("id").get<std::string>();
                    m.conversationId     = data.at("conversationId").get<std::string>();
                    m.senderUsername     = data.value("senderUsername", "[deleted]");
                    m.ciphertext         = data.at("ciphertext").get<std::string>();
                    m.nonce              = data.at("nonce").get<std::string>();
                    m.ephemeralPublicKey = data.at("ephemeralPublicKey").get<std::string>();
                    m.timestamp          = data.value("timestamp", "");
                    if (m.conversationId == conv.id && m.senderUsername != client.username()) {
                        std::string text;
                        try { text = client.decryptMessage(m); } catch (...) { text = "[encrypted]"; }
                        // Erase the dangling prompt line, print message, restore prompt
                        std::cout << CLEAR_LINE;
                        printMessage(shortTime(m.timestamp), m.senderUsername, text, false);
                        std::cout << promptStr << std::flush;
                    }
                }
            } catch (...) {}
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv{0, 500'000};
        int ready = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);

        if (ready > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            std::string line;
            if (!std::getline(std::cin, line)) break;
            if (line == "/quit") break;
            if (line.empty()) { std::cout << promptStr << std::flush; continue; }

            // Move cursor up one line and erase it so the typed "user> text"
            // is replaced by the formatted message line.
            std::cout << "\033[1A" << CLEAR_LINE;

            if (client.sendMessage(conv.id, line, recipientUser->publicKey)) {
                // Show our own message in the same style as received ones
                // (timestamp will be approximate — server timestamp isn't returned here)
                auto now = std::time(nullptr);
                char tbuf[6];
                std::strftime(tbuf, sizeof(tbuf), "%H:%M", std::localtime(&now));
                printMessage(tbuf, client.username(), line, true);
            } else {
                std::cout << "[send failed]\n";
            }
            std::cout << promptStr << std::flush;
        }
    }
    std::cout << CLEAR_LINE << "Left chat.\n";
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
                  << "  [2] Chat\n"
                  << "  [3] New conversation\n"
                  << "  [4] Revoke access\n"
                  << "  [5] Account settings\n"
                  << "  [q] Logout & quit\n";
        printSeparator();

        auto c = prompt("> ");
        if      (c == "1") menuMessages(client);
        else if (c == "2") menuLiveChat(client);
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
