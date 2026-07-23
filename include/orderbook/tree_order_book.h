#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "orderbook/event.h"
#include "orderbook/trade.h"

namespace orderbook {

struct RestingOrder {
    uint64_t order_id;
    Side side;
    int64_t price;
    uint32_t quantity;
    uint64_t timestamp_ns;
};

// Single-threaded limit order book for now
class TreeOrderBook {
public:
    // dispatches a feed event to add/cancel/modify
    std::vector<Trade> apply(const OrderEvent& event);

    std::vector<Trade> addOrder(uint64_t order_id, Side side, int64_t price,
                                 uint32_t quantity, uint64_t timestamp_ns);
    bool cancelOrder(uint64_t order_id);
    std::vector<Trade> modifyOrder(uint64_t order_id, int64_t new_price,
                                    uint32_t new_quantity, uint64_t timestamp_ns);

    std::optional<int64_t> bestBid() const;
    std::optional<int64_t> bestAsk() const;
    uint32_t quantityAt(Side side, int64_t price) const;
    size_t orderCount() const { return order_index_.size(); }

private:
    using Level = std::list<RestingOrder>;

    struct OrderLocation {
        Side side;
        int64_t price;
        Level::iterator iterator;
    };

    // fills against 'opposite' while crosses(level_price, order_price) holds
    template <typename BookMap, typename Crosses>
    uint32_t matchAgainst(BookMap& opposite, uint64_t taker_id, int64_t price,
                           uint32_t remaining, Crosses crosses,
                           std::vector<Trade>& trades);

    void rest(Side side, int64_t price, uint64_t order_id, uint32_t quantity,
              uint64_t timestamp_ns);

    // bids ordered highest-first, asks lowest-first
    std::map<int64_t, Level, std::greater<int64_t>> bids_;
    std::map<int64_t, Level> asks_;
    std::unordered_map<uint64_t, OrderLocation> order_index_;
};

} // namespace orderbook