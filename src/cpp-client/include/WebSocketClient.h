#pragma once
#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <ixwebsocket/IXWebSocket.h>

// Thread-safe WebSocket client.
// ixwebsocket manages the connection on a background thread; received messages
// are pushed into a queue and consumed from the main thread via poll().
class WebSocketClient {
public:
    WebSocketClient() = default;
    ~WebSocketClient() { disconnect(); }

    WebSocketClient(const WebSocketClient&)            = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    // Connect to `url` and start the background receive loop.
    void connect(const std::string& url);

    // Stop the background thread and close the socket.
    void disconnect();

    // Send a text frame. Returns false if not connected.
    [[nodiscard]] bool send(const std::string& message);

    // Non-blocking: returns the next queued message, or "" if empty.
    [[nodiscard]] std::string poll();

    bool isConnected() const noexcept { return _connected.load(); }

private:
    ix::WebSocket        _ws;
    std::queue<std::string> _queue;
    std::mutex           _mutex;
    std::atomic<bool>    _connected{false};
};
