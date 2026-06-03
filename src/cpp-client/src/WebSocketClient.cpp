#include "WebSocketClient.h"
#include <iostream>

void WebSocketClient::connect(const std::string& url) {
    _ws.setUrl(url);

    // TLS: verify the server's certificate (never skip in production)
    ix::SocketTLSOptions tls;
    tls.caFile = "SYSTEM"; // use the OS certificate store
    _ws.setTLSOptions(tls);

    _ws.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        switch (msg->type) {
            case ix::WebSocketMessageType::Open:
                _connected = true;
                std::cerr << "[WS] Connected\n";
                break;

            case ix::WebSocketMessageType::Close:
                _connected = false;
                std::cerr << "[WS] Closed: " << msg->closeInfo.reason << "\n";
                break;

            case ix::WebSocketMessageType::Error:
                _connected = false;
                std::cerr << "[WS] Error: " << msg->errorInfo.reason << "\n";
                break;

            case ix::WebSocketMessageType::Message: {
                std::lock_guard<std::mutex> lock(_mutex);
                _queue.push(msg->str);
                break;
            }

            default:
                break;
        }
    });

    _ws.start();
}

void WebSocketClient::disconnect() {
    _ws.stop();
    _connected = false;
}

bool WebSocketClient::send(const std::string& message) {
    if (!_connected) return false;
    auto result = _ws.send(message);
    return result.success;
}

std::string WebSocketClient::poll() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_queue.empty()) return {};
    auto msg = std::move(_queue.front());
    _queue.pop();
    return msg;
}
