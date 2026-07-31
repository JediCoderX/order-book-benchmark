#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

#include "orderbook/event.h"
#include "orderbook/flat_order_book.h"
#include "orderbook/trade.h"

namespace orderbook {

// Thread-safe wrapper around FlatOrderBook, one mutex locked around every operation
class CoarseLockOrderBook {
public:
    CoarseLockOrderBook(int64_t min_price, size_t price_range, size_t pool_capacity, uint64_t max_order_id);

    std::vector<Trade> apply(const OrderEvent& event);

    std::vector<Trade> addOrder(uint64_t order_id, Side side, int64_t price, uint32_t quantity, uint64_t timestamp_ns);
    bool cancelOrder(uint64_t order_id);
    std::vector<Trade> modifyOrder(uint64_t order_id, int64_t new_price, uint32_t new_quantity, uint64_t timestamp_ns);

    std::optional<int64_t> bestBid() const;
    std::optional<int64_t> bestAsk() const;
    uint32_t quantityAt(Side side, int64_t price) const;

private:
    mutable std::mutex mutex_;
    FlatOrderBook book_;
};

} // namespace orderbook