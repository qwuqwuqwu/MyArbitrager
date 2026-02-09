#include "binance_client.hpp"
#include "thread_affinity.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>

BinanceWebSocketClient::BinanceWebSocketClient()
    : connected_(false)
    , should_stop_(false)
    , message_count_(0) {
}

BinanceWebSocketClient::~BinanceWebSocketClient() {
    disconnect();
}

bool BinanceWebSocketClient::connect(const std::vector<std::string>& symbols) {
    if (connected_) {
        std::cerr << "Already connected!" << std::endl;
        return false;
    }

    subscribed_symbols_ = symbols;

    try {
        // Build WebSocket URL with combined streams
        std::string streams;
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) streams += "/";
            streams += symbol_to_stream(symbols[i]);
        }

        // Use Binance.US endpoint (comment out to use regular Binance)
        std::string host = "stream.binance.us";
        std::string port = "9443";
        std::string target = "/stream?streams=" + streams;

        std::cout << "Connecting to: wss://" << host << ":" << port << target << std::endl;

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
            std::cerr << "SSL handshake failed: " << ec.message() << std::endl;
            return false;
        }

        // Set WebSocket options
        ws_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws_->set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(http::field::user_agent, "Mozilla/5.0");
            }));

        // Perform WebSocket handshake (use hostname only, not with port)
        ws_->handshake(host, target, ec);
        if (ec) {
            std::cerr << "WebSocket handshake failed: " << ec.message() << std::endl;
            std::cerr << "Error category: " << ec.category().name() << std::endl;
            std::cerr << "Error value: " << ec.value() << std::endl;
            return false;
        }

        connected_ = true;
        std::cout << "WebSocket connected successfully!" << std::endl;

        // Start reading thread
        ws_thread_ = std::thread(&BinanceWebSocketClient::run_client, this);

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Connection exception: " << e.what() << std::endl;
        connected_ = false;
        return false;
    }
}

void BinanceWebSocketClient::disconnect() {
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
                std::cerr << "Close error: " << ec.message() << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Disconnect exception: " << e.what() << std::endl;
    }

    if (ioc_) {
        ioc_->stop();
    }

    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
}

void BinanceWebSocketClient::set_message_callback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    message_callback_ = callback;
}

void BinanceWebSocketClient::run_client() {
    thread_affinity::set_thread_affinity(thread_affinity::TAG_BINANCE_WS);

    try {
        while (!should_stop_ && connected_) {
            do_read();
        }
    } catch (const std::exception& e) {
        std::cerr << "WebSocket read error: " << e.what() << std::endl;
        connected_ = false;
    }
}

void BinanceWebSocketClient::do_read() {
    try {
        buffer_.clear();
        beast::error_code ec;
        ws_->read(buffer_, ec);

        if (ec) {
            if (ec != websocket::error::closed) {
                std::cerr << "Read error: " << ec.message() << std::endl;
            }
            connected_ = false;
            return;
        }

        on_read(ec, buffer_.size());

    } catch (const std::exception& e) {
        std::cerr << "Read exception: " << e.what() << std::endl;
        connected_ = false;
    }
}

void BinanceWebSocketClient::on_read(beast::error_code ec, std::size_t bytes_transferred) {
    if (ec) {
        return;
    }

    message_count_++;

    try {
        std::string message = beast::buffers_to_string(buffer_.data());
        parse_ticker_message(message);
    } catch (const std::exception& e) {
        std::cerr << "Message parsing error: " << e.what() << std::endl;
    }
}

void BinanceWebSocketClient::parse_ticker_message(const std::string& message) {
    try {
        auto j = json::parse(message);

        // Binance combined stream format: { "stream": "...", "data": {...} }
        if (!j.contains("data") || !j.contains("stream")) {
            return;  // Not a data message
        }

        // Check if this is a bookTicker stream
        std::string stream = j["stream"].get<std::string>();
        if (stream.find("@bookTicker") == std::string::npos) {
            return;
        }

        auto data = j["data"];

        // Parse ticker data
        TickerData ticker;
        ticker.symbol = data["s"].get<std::string>();
        ticker.exchange = "Binance";
        ticker.bid_price = std::stod(data["b"].get<std::string>());
        ticker.ask_price = std::stod(data["a"].get<std::string>());
        ticker.bid_quantity = std::stod(data["B"].get<std::string>());
        ticker.ask_quantity = std::stod(data["A"].get<std::string>());

        // Set timestamp
        auto now = std::chrono::system_clock::now();
        ticker.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();

        // Call the callback with the new ticker data
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (message_callback_) {
                message_callback_(ticker);
            }
        }

    } catch (const json::exception& e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Ticker parsing error: " << e.what() << std::endl;
    }
}

std::string BinanceWebSocketClient::symbol_to_stream(const std::string& symbol) {
    // Convert "BTCUSDT" to "btcusdt@bookTicker"
    std::string lower_symbol = symbol;
    std::transform(lower_symbol.begin(), lower_symbol.end(), lower_symbol.begin(), ::tolower);
    return lower_symbol + "@bookTicker";
}
