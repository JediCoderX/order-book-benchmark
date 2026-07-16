#include <iostream>

#include "orderbook/feed_parser.h"
#include "orderbook/order_book.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: orderbook_replay <feed_file.csv>\n";
        return 1;
    }

    orderbook::FeedParser parser(argv[1]);
    orderbook::OrderBook book;
    orderbook::OrderEvent event{};

    uint64_t events_applied = 0;
    uint64_t trades_generated = 0;

    while (parser.next(event)) {
        auto trades = book.apply(event);
        ++events_applied;
        for (const auto& trade : trades) {
            std::cout << "TRADE price=" << trade.price << " qty=" << trade.quantity
                      << " taker=" << trade.taker_order_id
                      << " maker=" << trade.maker_order_id << "\n";
            ++trades_generated;
        }
    }

    std::cout << "--- replay complete ---\n"
              << "events applied:   " << events_applied << "\n"
              << "trades generated: " << trades_generated << "\n";
    if (auto bid = book.bestBid()) {
        std::cout << "best bid: " << *bid << "\n";
    } else {
        std::cout << "best bid: (none)\n";
    }
    if (auto ask = book.bestAsk()) {
        std::cout << "best ask: " << *ask << "\n";
    } else {
        std::cout << "best ask: (none)\n";
    }

    return 0;
}