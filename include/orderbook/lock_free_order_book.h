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

// lock-free price levels (Michael-Scott enqueue, Harris-style mark-then-lazy-unlink for cancel) over a grow-only pool that never reuses slots
class LockFreeOrderBook {
public:
    // pool_capacity bounds total orders ever created, since slots are never reused
    LockFreeOrderBook(int64_t min_price, size_t price_range, size_t pool_capacity, uint64_t max_order_id);

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
        uint64_t order_id = 0;
        int64_t price = 0;
        std::atomic<uint32_t> quantity{0};
        uint64_t timestamp_ns = 0;
        Side side = Side::Buy;
        std::atomic<bool> cancelled{false};
        std::atomic<uint32_t> next{kNil};  // list link; dummy nodes use this too
    };

    // head is always a dummy node, real front is head.next
    // Occupancy is tracked via total_quantity
    struct PriceLevel {
        std::atomic<uint32_t> head{kNil};
        std::atomic<uint32_t> tail{kNil};
        std::atomic<int64_t> total_quantity{0};
    };

    uint32_t allocateSlot();
    void ensureDummy(PriceLevel& level);
    void pushBack(PriceLevel& level, uint32_t slot);
    static uint32_t claimQuantity(std::atomic<uint32_t>& qty, uint32_t want);
    // walks from the front, skipping/unlinking cancelled or exhausted orders
    uint32_t matchLevel(PriceLevel& level, uint64_t taker_id, uint32_t remaining, std::vector<Trade>& trades);
    void rest(Side side, int64_t price, uint64_t order_id, uint32_t quantity, uint64_t timestamp_ns);

    // caller must already hold best_mutex_
    void advanceBestBid(int64_t from_idx);
    void advanceBestAsk(int64_t from_idx);

    int64_t min_price_;
    size_t price_range_;

    std::vector<PriceLevel> bid_levels_;
    std::vector<PriceLevel> ask_levels_;

    mutable std::mutex best_mutex_;  // not bottleneck, so not worth going lock-free here
    int64_t best_bid_idx_ = -1;
    int64_t best_ask_idx_ = -1;

    std::vector<PooledOrder> pool_;
    std::atomic<uint32_t> next_free_slot_{0};

    std::vector<std::atomic<uint32_t>> id_to_slot_;
};

} // namespace orderbook
