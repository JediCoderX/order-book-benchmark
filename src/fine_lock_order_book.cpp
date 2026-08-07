#include "orderbook/fine_lock_order_book.h"

#include <algorithm>
#include <stdexcept>

namespace orderbook {

FineLockOrderBook::FineLockOrderBook(int64_t min_price, size_t price_range, size_t pool_capacity, uint64_t max_order_id)
    : min_price_(min_price),
      price_range_(price_range),
      bid_levels_(price_range),
      ask_levels_(price_range),
      pool_(pool_capacity),
      id_to_slot_(max_order_id) {
    for (size_t i = 0; i < pool_capacity; ++i) {
        pool_[i].next = (i + 1 < pool_capacity) ? static_cast<uint32_t>(i + 1) : kNil;
    }
    free_head_ = pool_capacity > 0 ? 0 : kNil;
    for (auto& slot : id_to_slot_) {
        slot.store(kNil, std::memory_order_relaxed);
    }
}

uint32_t FineLockOrderBook::allocateSlot() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    if (free_head_ == kNil) {
        throw std::runtime_error("FineLockOrderBook: order pool exhausted");
    }
    uint32_t slot = free_head_;
    free_head_ = pool_[slot].next;
    return slot;
}

void FineLockOrderBook::freeSlot(uint32_t slot) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    pool_[slot].next = free_head_;
    free_head_ = slot;
}

void FineLockOrderBook::pushBack(PriceLevel& level, uint32_t slot) {
    PooledOrder& order = pool_[slot];
    order.prev = level.tail;
    order.next = kNil;
    if (level.tail != kNil) {
        pool_[level.tail].next = slot;
    } else {
        level.head = slot;
    }
    level.tail = slot;
}

void FineLockOrderBook::unlink(PriceLevel& level, uint32_t slot) {
    PooledOrder& order = pool_[slot];
    if (order.prev != kNil) {
        pool_[order.prev].next = order.next;
    } else {
        level.head = order.next;
    }
    if (order.next != kNil) {
        pool_[order.next].prev = order.prev;
    } else {
        level.tail = order.prev;
    }
}

void FineLockOrderBook::advanceBestAsk(int64_t from_idx) {
    // lock-free peek at each level's occupancy
    int64_t next = from_idx + 1;
    while (next < static_cast<int64_t>(price_range_) && ask_levels_[next].head == kNil) {
        ++next;
    }
    best_ask_idx_ = (next < static_cast<int64_t>(price_range_)) ? next : -1;
}

void FineLockOrderBook::advanceBestBid(int64_t from_idx) {
    int64_t next = from_idx - 1;
    while (next >= 0 && bid_levels_[next].head == kNil) {
        --next;
    }
    best_bid_idx_ = next;
}

void FineLockOrderBook::rest(Side side, int64_t price, uint64_t order_id, uint32_t quantity, uint64_t timestamp_ns) {
    int64_t idx = price - min_price_;
    uint32_t slot = allocateSlot();
    PooledOrder& order = pool_[slot];
    order.order_id = order_id;
    order.price.store(price, std::memory_order_relaxed);
    order.quantity = quantity;
    order.timestamp_ns = timestamp_ns;
    order.side.store(side, std::memory_order_relaxed);
    order.prev = kNil;
    order.next = kNil;
    id_to_slot_[order_id].store(slot, std::memory_order_release);

    if (side == Side::Buy) {
        PriceLevel& level = bid_levels_[idx];
        {
            std::lock_guard<std::mutex> lock(level.mutex);
            pushBack(level, slot);
            level.total_quantity += quantity;
        }
        std::lock_guard<std::mutex> lock(best_mutex_);
        if (best_bid_idx_ == -1 || idx > best_bid_idx_) {
            best_bid_idx_ = idx;
        }
    } else {
        PriceLevel& level = ask_levels_[idx];
        {
            std::lock_guard<std::mutex> lock(level.mutex);
            pushBack(level, slot);
            level.total_quantity += quantity;
        }
        std::lock_guard<std::mutex> lock(best_mutex_);
        if (best_ask_idx_ == -1 || idx < best_ask_idx_) {
            best_ask_idx_ = idx;
        }
    }
}

std::vector<Trade> FineLockOrderBook::addOrder(uint64_t order_id, Side side, int64_t price, uint32_t quantity, uint64_t timestamp_ns) {
    int64_t idx = price - min_price_;
    if (idx < 0 || idx >= static_cast<int64_t>(price_range_)) {
        throw std::out_of_range("FineLockOrderBook: price outside configured range");
    }
    if (order_id >= id_to_slot_.size()) {
        throw std::out_of_range("FineLockOrderBook: order_id outside configured capacity");
    }

    std::vector<Trade> trades;
    uint32_t remaining = quantity;

    if (side == Side::Buy) {
        while (remaining > 0) {
            int64_t ask_idx;
            {
                std::lock_guard<std::mutex> lock(best_mutex_);
                ask_idx = best_ask_idx_;
            }
            if (ask_idx == -1 || ask_idx > idx) {
                break;
            }

            PriceLevel& level = ask_levels_[ask_idx];
            bool drained_empty;
            {
                std::lock_guard<std::mutex> lock(level.mutex);
                while (remaining > 0 && level.head != kNil) {
                    uint32_t maker_slot = level.head;
                    PooledOrder& maker = pool_[maker_slot];
                    uint32_t traded = std::min(remaining, maker.quantity);
                    trades.push_back(Trade{order_id, maker.order_id, maker.price, traded});
                    remaining -= traded;
                    maker.quantity -= traded;
                    level.total_quantity -= traded;
                    if (maker.quantity == 0) {
                        unlink(level, maker_slot);
                        id_to_slot_[maker.order_id].store(kNil, std::memory_order_release);
                        freeSlot(maker_slot);
                    }
                }
                drained_empty = (level.head == kNil);
            }
            if (drained_empty) {
                std::lock_guard<std::mutex> lock(best_mutex_);
                if (best_ask_idx_ == ask_idx) {
                    advanceBestAsk(ask_idx);
                }
            }
        }
    } else {
        while (remaining > 0) {
            int64_t bid_idx;
            {
                std::lock_guard<std::mutex> lock(best_mutex_);
                bid_idx = best_bid_idx_;
            }
            if (bid_idx == -1 || bid_idx < idx) {
                break;
            }

            PriceLevel& level = bid_levels_[bid_idx];
            bool drained_empty;
            {
                std::lock_guard<std::mutex> lock(level.mutex);
                while (remaining > 0 && level.head != kNil) {
                    uint32_t maker_slot = level.head;
                    PooledOrder& maker = pool_[maker_slot];
                    uint32_t traded = std::min(remaining, maker.quantity);
                    trades.push_back(Trade{order_id, maker.order_id, maker.price, traded});
                    remaining -= traded;
                    maker.quantity -= traded;
                    level.total_quantity -= traded;
                    if (maker.quantity == 0) {
                        unlink(level, maker_slot);
                        id_to_slot_[maker.order_id].store(kNil, std::memory_order_release);
                        freeSlot(maker_slot);
                    }
                }
                drained_empty = (level.head == kNil);
            }
            if (drained_empty) {
                std::lock_guard<std::mutex> lock(best_mutex_);
                if (best_bid_idx_ == bid_idx) {
                    advanceBestBid(bid_idx);
                }
            }
        }
    }

    if (remaining > 0) {
        rest(side, price, order_id, remaining, timestamp_ns);
    }
    return trades;
}

bool FineLockOrderBook::cancelOrder(uint64_t order_id) {
    if (order_id >= id_to_slot_.size()) {
        return false;
    }
    uint32_t slot = id_to_slot_[order_id].load(std::memory_order_acquire);
    if (slot == kNil) {
        return false;
    }

    // side/price are just a starting guess for which lock to take -- the
    // slot could be concurrently filled and reused before we get the lock,
    // so nothing here is trusted until the id_to_slot_ check below passes.
    Side side = pool_[slot].side.load(std::memory_order_acquire);
    int64_t price = pool_[slot].price.load(std::memory_order_acquire);
    int64_t idx = price - min_price_;

    bool drained_empty;
    bool still_valid;
    if (side == Side::Buy) {
        PriceLevel& level = bid_levels_[idx];
        {
            std::lock_guard<std::mutex> lock(level.mutex);
            still_valid = (id_to_slot_[order_id].load(std::memory_order_acquire) == slot);
            if (still_valid) {
                level.total_quantity -= pool_[slot].quantity;
                unlink(level, slot);
                drained_empty = (level.head == kNil);
            }
        }
        if (!still_valid) {
            return false;  // filled concurrently between our read and the lock
        }
        id_to_slot_[order_id].store(kNil, std::memory_order_release);
        freeSlot(slot);
        if (drained_empty) {
            std::lock_guard<std::mutex> lock(best_mutex_);
            if (best_bid_idx_ == idx) {
                advanceBestBid(idx);
            }
        }
    } else {
        PriceLevel& level = ask_levels_[idx];
        {
            std::lock_guard<std::mutex> lock(level.mutex);
            still_valid = (id_to_slot_[order_id].load(std::memory_order_acquire) == slot);
            if (still_valid) {
                level.total_quantity -= pool_[slot].quantity;
                unlink(level, slot);
                drained_empty = (level.head == kNil);
            }
        }
        if (!still_valid) {
            return false;
        }
        id_to_slot_[order_id].store(kNil, std::memory_order_release);
        freeSlot(slot);
        if (drained_empty) {
            std::lock_guard<std::mutex> lock(best_mutex_);
            if (best_ask_idx_ == idx) {
                advanceBestAsk(idx);
            }
        }
    }
    return true;
}

std::vector<Trade> FineLockOrderBook::modifyOrder(uint64_t order_id, int64_t new_price, uint32_t new_quantity, uint64_t timestamp_ns) {
    if (order_id >= id_to_slot_.size()) {
        return {};
    }
    uint32_t slot = id_to_slot_[order_id].load(std::memory_order_acquire);
    if (slot == kNil) {
        return {};
    }
    Side side = pool_[slot].side.load(std::memory_order_acquire);
    if (!cancelOrder(order_id)) {
        return {};  // already gone (raced with a concurrent fill)
    }
    return addOrder(order_id, side, new_price, new_quantity, timestamp_ns);
}

std::vector<Trade> FineLockOrderBook::apply(const OrderEvent& event) {
    switch (event.type) {
        case EventType::Add:
            return addOrder(event.order_id, event.side, event.price, event.quantity, event.timestamp_ns);
        case EventType::Cancel:
            cancelOrder(event.order_id);
            return {};
        case EventType::Modify:
            return modifyOrder(event.order_id, event.price, event.quantity, event.timestamp_ns);
    }
    return {};
}

std::optional<int64_t> FineLockOrderBook::bestBid() const {
    std::lock_guard<std::mutex> lock(best_mutex_);
    if (best_bid_idx_ == -1) {
        return std::nullopt;
    }
    return min_price_ + best_bid_idx_;
}

std::optional<int64_t> FineLockOrderBook::bestAsk() const {
    std::lock_guard<std::mutex> lock(best_mutex_);
    if (best_ask_idx_ == -1) {
        return std::nullopt;
    }
    return min_price_ + best_ask_idx_;
}

uint32_t FineLockOrderBook::quantityAt(Side side, int64_t price) const {
    int64_t idx = price - min_price_;
    if (idx < 0 || idx >= static_cast<int64_t>(price_range_)) {
        return 0;
    }
    const PriceLevel& level = (side == Side::Buy) ? bid_levels_[idx] : ask_levels_[idx];
    std::lock_guard<std::mutex> lock(level.mutex);
    return static_cast<uint32_t>(level.total_quantity);
}

} // namespace orderbook