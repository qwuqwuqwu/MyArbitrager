#pragma once

#include "types.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <mutex>
#include <memory>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

class CoinbaseWebSocketClient {
public:
    using MessageCallback = std::function<void(const TickerData&)>;

    CoinbaseWebSocketClient();
    ~CoinbaseWebSocketClient();

    // Connect to Coinbase WebSocket and subscribe to ticker channel
    bool connect(const std::vector<std::string>& symbols);

    // Disconnect and cleanup
    void disconnect();

    // Set callback for when new ticker data arrives
    void set_message_callback(MessageCallback callback);

    // Check if client is connected
    bool is_connected() const { return connected_; }

    // Get connection statistics
    uint64_t get_message_count() const { return message_count_; }

private:
    std::unique_ptr<net::io_context> ioc_;
    std::unique_ptr<ssl::context> ctx_;
    std::unique_ptr<websocket::stream<beast::ssl_stream<tcp::socket>>> ws_;

    std::thread ws_thread_;
    std::atomic<bool> connected_;
    std::atomic<bool> should_stop_;
    std::atomic<uint64_t> message_count_;

    MessageCallback message_callback_;
    std::mutex callback_mutex_;

    std::vector<std::string> subscribed_symbols_;
    beast::flat_buffer buffer_;

    // WebSocket operations
    void run_client();
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);

    // Parse Coinbase ticker message
    void parse_ticker_message(const std::string& message);

    // Convert symbol format (e.g., "BTCUSDT" to "BTC-USD" for Coinbase)
    std::string binance_to_coinbase_symbol(const std::string& symbol);

    // Send subscription message
    void send_subscribe_message(const std::vector<std::string>& symbols);
};
