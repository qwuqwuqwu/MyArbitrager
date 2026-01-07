#include "kraken_client.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>

KrakenWebSocketClient::KrakenWebSocketClient()
    : connected_(false)
    , should_stop_(false)
    , message_count_(0) {
}

KrakenWebSocketClient::~KrakenWebSocketClient() {
    disconnect();
}

bool KrakenWebSocketClient::connect(const std::vector<std::string>& symbols) {
    if (connected_) {
        std::cerr << "Already connected!" << std::endl;
        return false;
    }

    subscribed_symbols_ = symbols;

    try {
        std::string host = "ws.kraken.com";
        std::string port = "443";
        std::string target = "/v2";

        std::cout << "Connecting to Kraken: wss://" << host << target << std::endl;

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
            std::cerr << "Kraken SSL handshake failed: " << ec.message() << std::endl;
            return false;
        }

        // Set WebSocket options
        ws_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws_->set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(http::field::user_agent, "Mozilla/5.0");
            }));

        // Perform WebSocket handshake
        ws_->handshake(host, target, ec);
        if (ec) {
            std::cerr << "Kraken WebSocket handshake failed: " << ec.message() << std::endl;
            return false;
        }

        connected_ = true;
        std::cout << "Kraken WebSocket connected successfully!" << std::endl;

        // Send subscription message
        send_subscribe_message(symbols);

        // Start reading thread
        ws_thread_ = std::thread(&KrakenWebSocketClient::run_client, this);

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Kraken connection exception: " << e.what() << std::endl;
        connected_ = false;
        return false;
    }
}

void KrakenWebSocketClient::disconnect() {
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
                std::cerr << "Kraken close error: " << ec.message() << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Kraken disconnect exception: " << e.what() << std::endl;
    }

    if (ioc_) {
        ioc_->stop();
    }

    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
}

void KrakenWebSocketClient::set_message_callback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    message_callback_ = callback;
}

void KrakenWebSocketClient::send_subscribe_message(const std::vector<std::string>& symbols) {
    try {
        // Convert Binance symbols to Kraken format
        std::vector<std::string> kraken_symbols;
        for (const auto& symbol : symbols) {
            kraken_symbols.push_back(binance_to_kraken_symbol(symbol));
        }

        // Build subscription message for Kraken v2 API
        // Use "bbo" event_trigger for faster updates (triggered by best bid/offer changes)
        json subscribe_msg = {
            {"method", "subscribe"},
            {"params", {
                {"channel", "ticker"},
                {"symbol", kraken_symbols},
                {"event_trigger", "bbo"}
            }}
        };

        std::string msg_str = subscribe_msg.dump();
        std::cout << "Sending Kraken subscription: " << msg_str << std::endl;

        // Send the subscription message
        ws_->write(net::buffer(msg_str));

    } catch (const std::exception& e) {
        std::cerr << "Failed to send Kraken subscription: " << e.what() << std::endl;
    }
}

void KrakenWebSocketClient::run_client() {
    try {
        while (!should_stop_ && connected_) {
            do_read();
        }
    } catch (const std::exception& e) {
        std::cerr << "Kraken WebSocket read error: " << e.what() << std::endl;
        connected_ = false;
    }
}

void KrakenWebSocketClient::do_read() {
    try {
        buffer_.clear();
        beast::error_code ec;
        ws_->read(buffer_, ec);

        if (ec) {
            if (ec != websocket::error::closed) {
                std::cerr << "Kraken read error: " << ec.message() << std::endl;
            }
            connected_ = false;
            return;
        }

        on_read(ec, buffer_.size());

    } catch (const std::exception& e) {
        std::cerr << "Kraken read exception: " << e.what() << std::endl;
        connected_ = false;
    }
}

void KrakenWebSocketClient::on_read(beast::error_code ec, std::size_t bytes_transferred) {
    if (ec) {
        return;
    }

    message_count_++;

    try {
        std::string message = beast::buffers_to_string(buffer_.data());
        parse_ticker_message(message);
    } catch (const std::exception& e) {
        std::cerr << "Kraken message parsing error: " << e.what() << std::endl;
    }
}

void KrakenWebSocketClient::parse_ticker_message(const std::string& message) {
    try {
        auto j = json::parse(message);

        // Kraken v2 sends various message types: method responses, channel data
        // Subscription confirmation has "method": "subscribe" and "success": true
        if (j.contains("method") && j["method"] == "subscribe") {
            if (j.contains("success") && j["success"] == true) {
                std::cout << "Kraken subscription confirmed" << std::endl;
            }
            return;
        }

        // Check if this is a ticker channel message
        if (!j.contains("channel") || j["channel"] != "ticker") {
            return;
        }

        // Check if we have data
        if (!j.contains("data")) {
            return;
        }

        // Kraken v2 ticker format has "data" array with ticker updates
        auto data_array = j["data"];
        if (data_array.empty()) {
            return;
        }

        // Process each ticker update
        for (const auto& ticker_data : data_array) {
            // Parse ticker data
            TickerData ticker;

            // Kraken uses "symbol" like "BTC/USD"
            std::string symbol = ticker_data["symbol"].get<std::string>();
            ticker.symbol = symbol;

            // Kraken provides bid and ask as numbers
            ticker.bid_price = ticker_data["bid"].get<double>();
            ticker.ask_price = ticker_data["ask"].get<double>();

            // Kraken provides bid_qty and ask_qty
            ticker.bid_quantity = ticker_data["bid_qty"].get<double>();
            ticker.ask_quantity = ticker_data["ask_qty"].get<double>();

            // Set timestamp
            auto now = std::chrono::system_clock::now();
            ticker.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()
            ).count();

            // Mark as Kraken exchange
            ticker.exchange = "Kraken";

            // Call the callback with the new ticker data
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (message_callback_) {
                    message_callback_(ticker);
                }
            }
        }

    } catch (const json::exception& e) {
        std::cerr << "Kraken JSON parsing error: " << e.what() << std::endl;
        std::cerr << "Message: " << message << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Kraken ticker parsing error: " << e.what() << std::endl;
    }
}

std::string KrakenWebSocketClient::binance_to_kraken_symbol(const std::string& symbol) {
    // Convert "BTCUSDT" to "BTC/USD" (Kraken format)
    // Handle common pairs
    if (symbol == "BTCUSDT") return "BTC/USD";
    if (symbol == "ETHUSDT") return "ETH/USD";
    if (symbol == "ADAUSDT") return "ADA/USD";
    if (symbol == "DOTUSDT") return "DOT/USD";
    if (symbol == "SOLUSDT") return "SOL/USD";
    if (symbol == "MATICUSDT") return "MATIC/USD";
    if (symbol == "AVAXUSDT") return "AVAX/USD";
    if (symbol == "LTCUSDT") return "LTC/USD";
    if (symbol == "LINKUSDT") return "LINK/USD";
    if (symbol == "XLMUSDT") return "XLM/USD";
    if (symbol == "XRPUSDT") return "XRP/USD";
    if (symbol == "UNIUSDT") return "UNI/USD";
    if (symbol == "AAVEUSDT") return "AAVE/USD";
    if (symbol == "ATOMUSDT") return "ATOM/USD";
    if (symbol == "ALGOUSDT") return "ALGO/USD";

    // Generic conversion: remove USDT and add /USD
    if (symbol.length() > 4 && symbol.substr(symbol.length() - 4) == "USDT") {
        return symbol.substr(0, symbol.length() - 4) + "/USD";
    }

    return symbol;  // Return as-is if no conversion found
}
