#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "orderbook/event.h"
#include "orderbook/trade.h"

namespace orderbook {

// No heap allocation after construction, price levels are a flat array
// indexed by price, orders live in a preallocated pool
class FlatOrderBook {
public:
    // pool_capacity bounds concurrent resting orders; max_order_id bounds
    // the id space, since ids index directly into an array
    FlatOrderBook(int64_t min_price, size_t price_range, size_t pool_capacity,
                  uint64_t max_order_id);

    std::vector<Trade> apply(const OrderEvent& event);

    std::vector<Trade> addOrder(uint64_t order_id, Side side, int64_t price,
                                 uint32_t quantity, uint64_t timestamp_ns);
    bool cancelOrder(uint64_t order_id);
    std::vector<Trade> modifyOrder(uint64_t order_id, int64_t new_price,
                                    uint32_t new_quantity, uint64_t timestamp_ns);

    std::optional<int64_t> bestBid() const;
    std::optional<int64_t> bestAsk() const;
    uint32_t quantityAt(Side side, int64_t price) const;

private:
    static constexpr uint32_t kNil = std::numeric_limits<uint32_t>::max();

    struct PooledOrder {
        uint64_t order_id;
        int64_t price;
        uint32_t quantity;
        uint64_t timestamp_ns;
        Side side;
        uint32_t prev;
        uint32_t next;  // free-list link when the slot is unused
    };

    struct PriceLevel {
        uint32_t head = kNil;
        uint32_t tail = kNil;
        uint64_t total_quantity = 0;
    };

    uint32_t allocateSlot();
    void freeSlot(uint32_t slot);
    void pushBack(PriceLevel& level, uint32_t slot);
    void unlink(PriceLevel& level, uint32_t slot);
    void rest(Side side, int64_t price, uint64_t order_id, uint32_t quantity,
              uint64_t timestamp_ns);

    int64_t min_price_;
    size_t price_range_;

    std::vector<PriceLevel> bid_levels_;
    std::vector<PriceLevel> ask_levels_;
    int64_t best_bid_idx_ = -1;  // no resting bids
    int64_t best_ask_idx_ = -1;  // no resting asks

    std::vector<PooledOrder> pool_;
    std::vector<uint32_t> id_to_slot_;  // order_id -> pool slot, kNil if not live
    uint32_t free_head_ = kNil;
};

} // namespace orderbook