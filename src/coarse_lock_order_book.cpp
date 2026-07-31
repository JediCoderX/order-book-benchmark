#include "orderbook/coarse_lock_order_book.h"

namespace orderbook {

CoarseLockOrderBook::CoarseLockOrderBook(int64_t min_price, size_t price_range, size_t pool_capacity, uint64_t max_order_id)
    : book_(min_price, price_range, pool_capacity, max_order_id) {}

std::vector<Trade> CoarseLockOrderBook::apply(const OrderEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    return book_.apply(event);
}

std::vector<Trade> CoarseLockOrderBook::addOrder(uint64_t order_id, Side side, int64_t price, uint32_t quantity, uint64_t timestamp_ns) {
    std::lock_guard<std::mutex> lock(mutex_);
    return book_.addOrder(order_id, side, price, quantity, timestamp_ns);
}

bool CoarseLockOrderBook::cancelOrder(uint64_t order_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return book_.cancelOrder(order_id);
}

std::vector<Trade> CoarseLockOrderBook::modifyOrder(uint64_t order_id, int64_t new_price, uint32_t new_quantity, uint64_t timestamp_ns) {
    std::lock_guard<std::mutex> lock(mutex_);
    return book_.modifyOrder(order_id, new_price, new_quantity, timestamp_ns);
}

std::optional<int64_t> CoarseLockOrderBook::bestBid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return book_.bestBid();
}

std::optional<int64_t> CoarseLockOrderBook::bestAsk() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return book_.bestAsk();
}

uint32_t CoarseLockOrderBook::quantityAt(Side side, int64_t price) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return book_.quantityAt(side, price);
}

} // namespace orderbook