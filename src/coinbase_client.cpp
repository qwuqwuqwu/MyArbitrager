#include "coinbase_client.hpp"
#include "thread_affinity.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>

CoinbaseWebSocketClient::CoinbaseWebSocketClient()
    : connected_(false)
    , should_stop_(false)
    , message_count_(0) {
}

CoinbaseWebSocketClient::~CoinbaseWebSocketClient() {
    disconnect();
}

bool CoinbaseWebSocketClient::connect(const std::vector<std::string>& symbols) {
    if (connected_) {
        std::cerr << "Already connected!" << std::endl;
        return false;
    }

    subscribed_symbols_ = symbols;

    try {
        std::string host = "advanced-trade-ws.coinbase.com";
        std::string port = "443";
        std::string target = "/";

        std::cout << "Connecting to Coinbase: wss://" << host << target << std::endl;

        // Initialize IO context and SSL context
        ioc_ = std::make_unique<net::io_context>();
        ctx_ = std::make_unique<ssl::context>(ssl::context::tlsv12_client);

        // Load root certificates
        ctx_->set_default_verify_paths();
        ctx_->set_verify_mode(ssl::verify_none); // For simplicity, skip verification

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
        ws_->next_layer().handshake(ssl::stream_base::client);

        // Set WebSocket options
        ws_->set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(http::field::user_agent, "Binance-Dashboard/1.0");
            }));

        // Perform WebSocket handshake
        ws_->handshake(host, target);

        connected_ = true;
        std::cout << "Coinbase WebSocket connected successfully!" << std::endl;

        // Send subscription message
        send_subscribe_message(symbols);

        // Start reading thread
        ws_thread_ = std::thread(&CoinbaseWebSocketClient::run_client, this);

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Coinbase connection exception: " << e.what() << std::endl;
        connected_ = false;
        return false;
    }
}

void CoinbaseWebSocketClient::disconnect() {
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
                std::cerr << "Coinbase close error: " << ec.message() << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Coinbase disconnect exception: " << e.what() << std::endl;
    }

    if (ioc_) {
        ioc_->stop();
    }

    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
}

void CoinbaseWebSocketClient::set_message_callback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    message_callback_ = callback;
}

void CoinbaseWebSocketClient::send_subscribe_message(const std::vector<std::string>& symbols) {
    try {
        // Convert Binance symbols to Coinbase format
        std::vector<std::string> coinbase_symbols;
        for (const auto& symbol : symbols) {
            coinbase_symbols.push_back(binance_to_coinbase_symbol(symbol));
        }

        // Build subscription message
        json subscribe_msg = {
            {"type", "subscribe"},
            {"product_ids", coinbase_symbols},
            {"channel", "ticker"}
        };

        std::string msg_str = subscribe_msg.dump();
        std::cout << "Sending Coinbase subscription: " << msg_str << std::endl;

        // Send the subscription message
        ws_->write(net::buffer(msg_str));

    } catch (const std::exception& e) {
        std::cerr << "Failed to send Coinbase subscription: " << e.what() << std::endl;
    }
}

void CoinbaseWebSocketClient::run_client() {
    thread_affinity::set_thread_affinity(thread_affinity::TAG_COINBASE_WS);

    try {
        while (!should_stop_ && connected_) {
            do_read();
        }
    } catch (const std::exception& e) {
        std::cerr << "Coinbase WebSocket read error: " << e.what() << std::endl;
        connected_ = false;
    }
}

void CoinbaseWebSocketClient::do_read() {
    try {
        buffer_.clear();
        beast::error_code ec;
        ws_->read(buffer_, ec);

        if (ec) {
            if (ec != websocket::error::closed) {
                std::cerr << "Coinbase read error: " << ec.message() << std::endl;
            }
            connected_ = false;
            return;
        }

        on_read(ec, buffer_.size());

    } catch (const std::exception& e) {
        std::cerr << "Coinbase read exception: " << e.what() << std::endl;
        connected_ = false;
    }
}

void CoinbaseWebSocketClient::on_read(beast::error_code ec, std::size_t bytes_transferred) {
    if (ec) {
        return;
    }

    message_count_++;

    try {
        std::string message = beast::buffers_to_string(buffer_.data());
        parse_ticker_message(message);
    } catch (const std::exception& e) {
        std::cerr << "Coinbase message parsing error: " << e.what() << std::endl;
    }
}

void CoinbaseWebSocketClient::parse_ticker_message(const std::string& message) {
    try {
        auto j = json::parse(message);

        // Coinbase sends various message types: subscriptions, ticker, etc.
        if (!j.contains("events")) {
            // Not a ticker data message (could be subscription confirmation)
            if (j.contains("type") && j["type"] == "subscriptions") {
                std::cout << "Coinbase subscription confirmed" << std::endl;
            }
            return;
        }

        // Coinbase ticker format has "events" array
        auto events = j["events"];
        if (events.empty()) {
            return;
        }

        // Process each event (typically just one per message)
        for (const auto& event : events) {
            if (!event.contains("tickers")) {
                continue;
            }

            auto tickers = event["tickers"];
            for (const auto& ticker_data : tickers) {
                // Parse ticker data
                TickerData ticker;

                // Coinbase uses "product_id" like "BTC-USD", convert to "BTCUSDT" format
                std::string product_id = ticker_data["product_id"].get<std::string>();
                ticker.symbol = product_id;  // Keep Coinbase format for now

                // Coinbase provides best_bid and best_ask
                ticker.bid_price = std::stod(ticker_data["best_bid"].get<std::string>());
                ticker.ask_price = std::stod(ticker_data["best_ask"].get<std::string>());

                // Coinbase provides best_bid_quantity and best_ask_quantity
                ticker.bid_quantity = std::stod(ticker_data["best_bid_quantity"].get<std::string>());
                ticker.ask_quantity = std::stod(ticker_data["best_ask_quantity"].get<std::string>());

                // Set timestamp
                auto now = std::chrono::system_clock::now();
                ticker.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()
                ).count();

                // Mark as Coinbase exchange
                ticker.exchange = "Coinbase";

                // Call the callback with the new ticker data
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    if (message_callback_) {
                        message_callback_(ticker);
                    }
                }
            }
        }

    } catch (const json::exception& e) {
        std::cerr << "Coinbase JSON parsing error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Coinbase ticker parsing error: " << e.what() << std::endl;
    }
}

std::string CoinbaseWebSocketClient::binance_to_coinbase_symbol(const std::string& symbol) {
    // Convert "BTCUSDT" to "BTC-USD" (Coinbase format)
    // Handle common pairs
    if (symbol == "BTCUSDT") return "BTC-USD";
    if (symbol == "ETHUSDT") return "ETH-USD";
    if (symbol == "BNBUSDT") return "BNB-USD";  // Note: BNB may not be on Coinbase
    if (symbol == "ADAUSDT") return "ADA-USD";
    if (symbol == "DOTUSDT") return "DOT-USD";
    if (symbol == "SOLUSDT") return "SOL-USD";
    if (symbol == "MATICUSDT") return "MATIC-USD";
    if (symbol == "AVAXUSDT") return "AVAX-USD";
    if (symbol == "LTCUSDT") return "LTC-USD";
    if (symbol == "LINKUSDT") return "LINK-USD";
    if (symbol == "XLMUSDT") return "XLM-USD";
    if (symbol == "XRPUSDT") return "XRP-USD";
    if (symbol == "UNIUSDT") return "UNI-USD";
    if (symbol == "AAVEUSDT") return "AAVE-USD";
    if (symbol == "ATOMUSDT") return "ATOM-USD";
    if (symbol == "ALGOUSDT") return "ALGO-USD";

    // Generic conversion: remove USDT and add -USD
    if (symbol.length() > 4 && symbol.substr(symbol.length() - 4) == "USDT") {
        return symbol.substr(0, symbol.length() - 4) + "-USD";
    }

    return symbol;  // Return as-is if no conversion found
}
