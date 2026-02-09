#include "bybit_client.hpp"
#include "thread_affinity.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>

BybitWebSocketClient::BybitWebSocketClient()
    : connected_(false)
    , should_stop_(false)
    , message_count_(0) {
}

BybitWebSocketClient::~BybitWebSocketClient() {
    disconnect();
}

bool BybitWebSocketClient::connect(const std::vector<std::string>& symbols) {
    if (connected_) {
        std::cerr << "Already connected!" << std::endl;
        return false;
    }

    subscribed_symbols_ = symbols;

    try {
        std::string host = "stream.bybit.com";
        std::string port = "443";
        std::string target = "/v5/public/spot";

        std::cout << "Connecting to Bybit: wss://" << host << target << std::endl;

        // Initialize IO context and SSL context
        ioc_ = std::make_unique<net::io_context>();
        ctx_ = std::make_unique<ssl::context>(ssl::context::tlsv12_client);

        // Load root certificates and configure SSL
        ctx_->set_default_verify_paths();
        ctx_->set_verify_mode(ssl::verify_none); // For simplicity, skip verification
        ctx_->set_options(ssl::context::default_workarounds |
                         ssl::context::no_sslv2 |
                         ssl::context::no_sslv3 |
                         ssl::context::single_dh_use);

        // Create resolver and WebSocket stream
        tcp::resolver resolver(*ioc_);
        ws_ = std::make_unique<websocket::stream<beast::ssl_stream<tcp::socket>>>(*ioc_, *ctx_);

        // Set SNI Hostname
        if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), host.c_str())) {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            std::cerr << "SSL SNI error: " << ec.message() << std::endl;
            return false;
        }

        // Look up the domain name
        auto const results = resolver.resolve(host, port);

        // Make the connection
        auto ep = net::connect(beast::get_lowest_layer(*ws_), results);

        // Perform SSL handshake
        beast::error_code ec;
        ws_->next_layer().handshake(ssl::stream_base::client, ec);
        if (ec) {
            std::cerr << "Bybit SSL handshake failed: " << ec.message() << std::endl;
            return false;
        }

        // Set WebSocket options — use keep_alive_pings for Bybit's 20s heartbeat
        websocket::stream_base::timeout opt;
        opt.idle_timeout = std::chrono::seconds(30);
        opt.handshake_timeout = std::chrono::seconds(10);
        opt.keep_alive_pings = true;  // Beast auto-sends WebSocket pings
        ws_->set_option(opt);

        ws_->set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(http::field::user_agent, "Mozilla/5.0");
            }));

        // Perform WebSocket handshake
        ws_->handshake(host, target, ec);
        if (ec) {
            std::cerr << "Bybit WebSocket handshake failed: " << ec.message() << std::endl;
            return false;
        }

        connected_ = true;
        std::cout << "Bybit WebSocket connected successfully!" << std::endl;

        // Send subscription message
        send_subscribe_message(symbols);

        // Start reading thread
        ws_thread_ = std::thread(&BybitWebSocketClient::run_client, this);

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Bybit connection exception: " << e.what() << std::endl;
        connected_ = false;
        return false;
    }
}

void BybitWebSocketClient::disconnect() {
    if (!connected_ && !ws_thread_.joinable()) {
        return;
    }

    should_stop_ = true;
    connected_ = false;

    try {
        if (ws_ && ws_->is_open()) {
            beast::error_code ec;
            ws_->close(websocket::close_code::normal, ec);
            if (ec) {
                std::cerr << "Bybit close error: " << ec.message() << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Bybit disconnect exception: " << e.what() << std::endl;
    }

    if (ioc_) {
        ioc_->stop();
    }

    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
}

void BybitWebSocketClient::set_message_callback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    message_callback_ = callback;
}

void BybitWebSocketClient::send_subscribe_message(const std::vector<std::string>& symbols) {
    try {
        // Convert Binance symbols to Bybit orderbook L1 topics
        std::vector<std::string> topics;
        for (const auto& symbol : symbols) {
            topics.push_back(binance_to_bybit_topic(symbol));
        }

        // Bybit allows max 10 args per subscription request
        // Split into batches if necessary
        const size_t BATCH_SIZE = 10;
        for (size_t i = 0; i < topics.size(); i += BATCH_SIZE) {
            size_t end = std::min(i + BATCH_SIZE, topics.size());
            std::vector<std::string> batch(topics.begin() + i, topics.begin() + end);

            json subscribe_msg = {
                {"req_id", std::to_string(i / BATCH_SIZE + 1)},
                {"op", "subscribe"},
                {"args", batch}
            };

            std::string msg_str = subscribe_msg.dump();
            std::cout << "Sending Bybit subscription (batch " << (i / BATCH_SIZE + 1) << "): "
                      << msg_str << std::endl;

            ws_->write(net::buffer(msg_str));
        }

    } catch (const std::exception& e) {
        std::cerr << "Failed to send Bybit subscription: " << e.what() << std::endl;
    }
}

void BybitWebSocketClient::run_client() {
    thread_affinity::set_thread_affinity(thread_affinity::TAG_BYBIT_WS);

    try {
        while (!should_stop_ && connected_) {
            do_read();
        }
    } catch (const std::exception& e) {
        std::cerr << "Bybit WebSocket read error: " << e.what() << std::endl;
        connected_ = false;
    }
}

void BybitWebSocketClient::do_read() {
    try {
        buffer_.clear();
        beast::error_code ec;
        ws_->read(buffer_, ec);

        if (ec) {
            if (ec != websocket::error::closed) {
                std::cerr << "Bybit read error: " << ec.message() << std::endl;
            }
            connected_ = false;
            return;
        }

        on_read(ec, buffer_.size());

    } catch (const std::exception& e) {
        std::cerr << "Bybit read exception: " << e.what() << std::endl;
        connected_ = false;
    }
}

void BybitWebSocketClient::on_read(beast::error_code ec, std::size_t bytes_transferred) {
    if (ec) {
        return;
    }

    message_count_++;

    try {
        std::string message = beast::buffers_to_string(buffer_.data());
        parse_ticker_message(message);
    } catch (const std::exception& e) {
        std::cerr << "Bybit message parsing error: " << e.what() << std::endl;
    }
}

void BybitWebSocketClient::parse_ticker_message(const std::string& message) {
    try {
        auto j = json::parse(message);

        // Handle subscription confirmation: {"success": true, "op": "subscribe", ...}
        if (j.contains("op")) {
            std::string op = j["op"].get<std::string>();
            if (op == "subscribe") {
                if (j.contains("success") && j["success"] == true) {
                    std::cout << "Bybit subscription confirmed" << std::endl;
                }
                return;
            }
            // Handle pong responses
            if (op == "pong") {
                return;
            }
        }

        // Check if this is an orderbook message
        if (!j.contains("topic")) {
            return;
        }

        std::string topic = j["topic"].get<std::string>();
        if (topic.find("orderbook.1.") == std::string::npos) {
            return;  // Not an orderbook L1 message
        }

        // Check if we have data
        if (!j.contains("data")) {
            return;
        }

        auto data = j["data"];

        // Validate bid and ask arrays exist and are non-empty
        if (!data.contains("b") || !data.contains("a") ||
            data["b"].empty() || data["a"].empty()) {
            return;
        }

        // Parse orderbook L1 data
        // Format: {"s": "BTCUSDT", "b": [["price", "size"]], "a": [["price", "size"]], ...}
        TickerData ticker;

        ticker.symbol = data["s"].get<std::string>();

        // BBO: first (and only) entry in bid/ask arrays
        ticker.bid_price = std::stod(data["b"][0][0].get<std::string>());
        ticker.bid_quantity = std::stod(data["b"][0][1].get<std::string>());
        ticker.ask_price = std::stod(data["a"][0][0].get<std::string>());
        ticker.ask_quantity = std::stod(data["a"][0][1].get<std::string>());

        // Set timestamp
        auto now = std::chrono::system_clock::now();
        ticker.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();

        // Mark as Bybit exchange
        ticker.exchange = "Bybit";

        // Call the callback with the new ticker data
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (message_callback_) {
                message_callback_(ticker);
            }
        }

    } catch (const json::exception& e) {
        std::cerr << "Bybit JSON parsing error: " << e.what() << std::endl;
        std::cerr << "Message: " << message << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Bybit ticker parsing error: " << e.what() << std::endl;
    }
}

std::string BybitWebSocketClient::binance_to_bybit_topic(const std::string& symbol) {
    // Bybit uses the same symbol format as Binance (BTCUSDT)
    // We subscribe to orderbook L1 (BBO only) for each symbol
    return "orderbook.1." + symbol;
}
