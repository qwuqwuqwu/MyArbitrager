#include "binance_client.hpp"
#include "coinbase_client.hpp"
#include "kraken_client.hpp"
#include "dashboard.hpp"
#include "arbitrage_engine.hpp"
#include <iostream>
#include <signal.h>
#include <vector>
#include <string>
#include <thread>

// Global variables for signal handling
std::unique_ptr<BinanceWebSocketClient> g_binance_client;
std::unique_ptr<CoinbaseWebSocketClient> g_coinbase_client;
std::unique_ptr<KrakenWebSocketClient> g_kraken_client;
std::unique_ptr<TerminalDashboard> g_dashboard;
std::unique_ptr<ArbitrageEngine> g_arbitrage_engine;
std::atomic<bool> g_shutdown(false);

void signal_handler(int signal) {
    std::cout << "\nShutdown signal received (" << signal << "). Cleaning up..." << std::endl;
    g_shutdown = true;

    if (g_arbitrage_engine) {
        g_arbitrage_engine->stop();
    }

    if (g_dashboard) {
        g_dashboard->stop();
    }

    if (g_binance_client) {
        g_binance_client->disconnect();
    }

    if (g_coinbase_client) {
        g_coinbase_client->disconnect();
    }

    if (g_kraken_client) {
        g_kraken_client->disconnect();
    }
}

void setup_signal_handlers() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifndef _WIN32
    signal(SIGQUIT, signal_handler);
#endif
}

int main() {
    std::cout << "Multi-Exchange Crypto Arbitrage Dashboard\n";
    std::cout << "==========================================\n\n";

    setup_signal_handlers();

    // Create dashboard, arbitrage engine, and WebSocket clients
    g_dashboard = std::make_unique<TerminalDashboard>();
    g_arbitrage_engine = std::make_unique<ArbitrageEngine>();
    g_binance_client = std::make_unique<BinanceWebSocketClient>();
    g_coinbase_client = std::make_unique<CoinbaseWebSocketClient>();
    g_kraken_client = std::make_unique<KrakenWebSocketClient>();

    // Define symbols to monitor (Binance format)
    std::vector<std::string> symbols = {
        "BTCUSDT",   // Bitcoin
        "ETHUSDT",   // Ethereum
        "ADAUSDT",   // Cardano
        "DOTUSDT",   // Polkadot
        "SOLUSDT",   // Solana
        "MATICUSDT", // Polygon
        "AVAXUSDT",  // Avalanche
        "LTCUSDT",   // Litecoin
        "LINKUSDT"   // Chainlink
    };

    std::cout << "Monitoring " << symbols.size() << " cryptocurrency pairs across 3 exchanges:\n";
    std::cout << "Binance.US + Coinbase + Kraken\n\n";

    // Set up callbacks to update dashboard and arbitrage engine when new data arrives
    g_binance_client->set_message_callback([&](const TickerData& ticker) {
        g_dashboard->update_market_data(ticker);
        g_arbitrage_engine->update_market_data(ticker);
    });

    g_coinbase_client->set_message_callback([&](const TickerData& ticker) {
        g_dashboard->update_market_data(ticker);
        g_arbitrage_engine->update_market_data(ticker);
    });

    g_kraken_client->set_message_callback([&](const TickerData& ticker) {
        g_dashboard->update_market_data(ticker);
        g_arbitrage_engine->update_market_data(ticker);
    });

    // Link dashboard to arbitrage engine so it can pull opportunities
    g_dashboard->set_arbitrage_engine(g_arbitrage_engine.get());

    std::cout << "Connecting to exchanges..." << std::endl;

    // Connect to Binance WebSocket
    if (!g_binance_client->connect(symbols)) {
        std::cerr << "Failed to connect to Binance WebSocket!" << std::endl;
        return 1;
    }

    // Connect to Coinbase WebSocket
    if (!g_coinbase_client->connect(symbols)) {
        std::cerr << "Failed to connect to Coinbase WebSocket!" << std::endl;
        // Continue anyway
    }

    // Connect to Kraken WebSocket
    if (!g_kraken_client->connect(symbols)) {
        std::cerr << "Failed to connect to Kraken WebSocket!" << std::endl;
        // Continue anyway
    }

    std::cout << "Connected successfully! Starting arbitrage engine and dashboard..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));  // Wait a bit for data to flow

    // Start the arbitrage engine (Thread 2)
    g_arbitrage_engine->set_min_profit_bps(5.0);  // 5 basis points minimum profit
    g_arbitrage_engine->set_calculation_interval(std::chrono::milliseconds(100));  // Calculate every 100ms
    g_arbitrage_engine->start();

    // Start the dashboard (Thread 1 - display)
    g_dashboard->set_update_interval(std::chrono::milliseconds(500)); // Update every 500ms
    g_dashboard->start();

    // Main application loop
    while (!g_shutdown && (g_binance_client->is_connected() ||
                           g_coinbase_client->is_connected() ||
                           g_kraken_client->is_connected())) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\nShutting down..." << std::endl;

    // Cleanup
    if (g_arbitrage_engine) {
        g_arbitrage_engine->stop();
    }

    if (g_dashboard) {
        g_dashboard->stop();
    }

    if (g_binance_client) {
        g_binance_client->disconnect();
    }

    if (g_coinbase_client) {
        g_coinbase_client->disconnect();
    }

    if (g_kraken_client) {
        g_kraken_client->disconnect();
    }

    std::cout << "Application stopped cleanly." << std::endl;
    return 0;
}
