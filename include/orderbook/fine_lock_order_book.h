#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <vector>

#include "orderbook/event.h"
#include "orderbook/trade.h"

namespace orderbook {

// One mutex per price level, plus separate mutexes for the best-index cache and the pool free list
// Lock order: best_mutex_ -> level -> pool_mutex_
//
// cancelOrder must read an order's side/price before it knows which level's
// mutex to take, and slots get reused (unlike LockFreeOrderBook's grow-only
// pool) -- so side/price/id_to_slot_ are atomic, and cancelOrder re-validates
// under the level lock before mutating, in case the order was concurrently
// filled and its slot freed/reused in the gap. Callers still must never
// touch the same order_id from two threads at once themselves.
class FineLockOrderBook {
public:
    FineLockOrderBook(int64_t min_price, size_t price_range, size_t pool_capacity, uint64_t max_order_id);

    std::vector<Trade> apply(const OrderEvent& event);

    std::vector<Trade> addOrder(uint64_t order_id, Side side, int64_t price, uint32_t quantity, uint64_t timestamp_ns);
    bool cancelOrder(uint64_t order_id);
    std::vector<Trade> modifyOrder(uint64_t order_id, int64_t new_price, uint32_t new_quantity, uint64_t timestamp_ns);

    std::optional<int64_t> bestBid() const;
    std::optional<int64_t> bestAsk() const;
    uint32_t quantityAt(Side side, int64_t price) const;

private:
    static constexpr uint32_t kNil = std::numeric_limits<uint32_t>::max();

    struct PooledOrder {
        uint64_t order_id;
        std::atomic<int64_t> price;
        uint32_t quantity;
        uint64_t timestamp_ns;
        std::atomic<Side> side;
        uint32_t prev;
        uint32_t next;  // free-list link when the slot is unused
    };

    struct PriceLevel {
        mutable std::mutex mutex;
        // atomic so advanceBestBid/Ask can peek at occupancy without locking
        std::atomic<uint32_t> head{kNil};
        uint32_t tail = kNil;
        uint64_t total_quantity = 0;
    };

    uint32_t allocateSlot();
    void freeSlot(uint32_t slot);
    void pushBack(PriceLevel& level, uint32_t slot);
    void unlink(PriceLevel& level, uint32_t slot);
    void rest(Side side, int64_t price, uint64_t order_id, uint32_t quantity, uint64_t timestamp_ns);

    // Caller must already hold best_mutex_
    // Scans lock-free via PriceLevel::head while looking for the next best price.
    void advanceBestBid(int64_t from_idx);
    void advanceBestAsk(int64_t from_idx);

    int64_t min_price_;
    size_t price_range_;

    std::vector<PriceLevel> bid_levels_;
    std::vector<PriceLevel> ask_levels_;

    mutable std::mutex best_mutex_;
    int64_t best_bid_idx_ = -1;
    int64_t best_ask_idx_ = -1;

    std::mutex pool_mutex_;
    std::vector<PooledOrder> pool_;
    uint32_t free_head_ = kNil;

    std::vector<std::atomic<uint32_t>> id_to_slot_;
};

} // namespace orderbook