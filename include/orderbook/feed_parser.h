#pragma once

#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include "orderbook/event.h"

namespace orderbook {

// CSV format: timestamp_ns,type,order_id,side,price,quantity
// type: A/C/M (add/cancel/modify), side: B/S (buy/sell)
std::optional<OrderEvent> parseLine(std::string_view line);

// Streams events from a file one at a time
class FeedParser {
public:
    explicit FeedParser(const std::string& path);

    // Returns false at EOF, skips malformed lines
    bool next(OrderEvent& out);

private:
    std::ifstream file_;
};

} // namespace orderbook