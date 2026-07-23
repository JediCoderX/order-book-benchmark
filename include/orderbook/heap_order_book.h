#pragma once

#include <cstdint>
#include <list>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

#include "orderbook/event.h"
#include "orderbook/trade.h"

namespace orderbook {

// Finds the best price via a heap instead of a tree or array
// No bounded price range needed, but cancels use lazy deletion
class HeapOrderBook {
public:
    std::vector<Trade> apply(const OrderEvent& event);

    std::vector<Trade> addOrder(uint64_t order_id, Side side, int64_t price,
                                 uint32_t quantity, uint64_t timestamp_ns);
    bool cancelOrder(uint64_t order_id);
    std::vector<Trade> modifyOrder(uint64_t order_id, int64_t new_price,
                                    uint32_t new_quantity, uint64_t timestamp_ns);

    // each call lazily pops stale entries off the relevant heap
    std::optional<int64_t> bestBid();
    std::optional<int64_t> bestAsk();
    uint32_t quantityAt(Side side, int64_t price) const;

private:
    struct RestingOrder {
        uint64_t order_id;
        Side side;
        int64_t price;
        uint32_t quantity;
        uint64_t timestamp_ns;
    };
    using Level = std::list<RestingOrder>;

    struct OrderLocation {
        Side side;
        int64_t price;
        Level::iterator iterator;
    };

    void rest(Side side, int64_t price, uint64_t order_id, uint32_t quantity,
              uint64_t timestamp_ns);
    void cleanBidTop();
    void cleanAskTop();

    std::unordered_map<int64_t, Level> bid_levels_;
    std::unordered_map<int64_t, Level> ask_levels_;
    std::priority_queue<int64_t> bid_heap_;  // max-heap: best bid = highest price
    std::priority_queue<int64_t, std::vector<int64_t>, std::greater<int64_t>>
        ask_heap_;  // min-heap: best ask = lowest price

    std::unordered_map<uint64_t, OrderLocation> order_index_;
};

} // namespace orderbook