#include "orderbook/lock_free_order_book.h"

#include <algorithm>
#include <stdexcept>

namespace orderbook {

namespace {
constexpr std::memory_order kAcq = std::memory_order_acquire;
constexpr std::memory_order kRel = std::memory_order_release;
constexpr std::memory_order kAcqRel = std::memory_order_acq_rel;
constexpr std::memory_order kRelaxed = std::memory_order_relaxed;
} // namespace

LockFreeOrderBook::LockFreeOrderBook(int64_t min_price, size_t price_range, size_t pool_capacity, uint64_t max_order_id)
    : min_price_(min_price),
      price_range_(price_range),
      bid_levels_(price_range),
      ask_levels_(price_range),
      pool_(pool_capacity),
      id_to_slot_(max_order_id) {
    for (auto& slot : id_to_slot_) {
        slot.store(kNil, kRelaxed);
    }
}

uint32_t LockFreeOrderBook::allocateSlot() {
    uint32_t slot = next_free_slot_.fetch_add(1, kRelaxed);
    if (slot >= pool_.size()) {
        throw std::runtime_error(
            "LockFreeOrderBook: order pool exhausted (grow-only, never reused)");
    }
    return slot;
}

void LockFreeOrderBook::ensureDummy(PriceLevel& level) {
    if (level.head.load(kAcq) != kNil) {
        return;
    }
    uint32_t dummy = allocateSlot();
    pool_[dummy].next.store(kNil, kRelaxed);
    pool_[dummy].cancelled.store(true, kRelaxed);
    uint32_t expected = kNil;
    if (level.head.compare_exchange_strong(expected, dummy, kAcqRel, kAcq)) {
        level.tail.store(dummy, kRel);
    }
    // if CAS failed, another thread already installed a dummy first
    // ours is simply unused (acceptable leak given the grow-only pool)
}

void LockFreeOrderBook::pushBack(PriceLevel& level, uint32_t slot) {
    ensureDummy(level);
    pool_[slot].next.store(kNil, kRelaxed);
    for (;;) {
        uint32_t last = level.tail.load(kAcq);
        uint32_t next = pool_[last].next.load(kAcq);
        if (last != level.tail.load(kAcq)) {
            continue;  // tail changed underneath us, retry with a fresh read
        }
        if (next == kNil) {
            if (pool_[last].next.compare_exchange_weak(next, slot, kAcqRel, kAcq)) {
                level.tail.compare_exchange_strong(last, slot, kAcqRel, kAcq);
                return;
            }
        } else {
            // tail is lagging behind an already-linked node, help move it forward
            level.tail.compare_exchange_strong(last, next, kAcqRel, kAcq);
        }
    }
}

uint32_t LockFreeOrderBook::claimQuantity(std::atomic<uint32_t>& qty, uint32_t want) {
    uint32_t current = qty.load(kAcq);
    for (;;) {
        if (current == 0) {
            return 0;
        }
        uint32_t claim = std::min(current, want);
        uint32_t new_val = current - claim;
        if (qty.compare_exchange_weak(current, new_val, kAcqRel, kAcq)) {
            return claim;
        }
    }
}

uint32_t LockFreeOrderBook::matchLevel(PriceLevel& level, uint64_t taker_id, uint32_t remaining, std::vector<Trade>& trades) {
    ensureDummy(level);
    for (;;) {
        if (remaining == 0) {
            return 0;
        }
        uint32_t head_slot = level.head.load(kAcq);
        uint32_t first_real = pool_[head_slot].next.load(kAcq);
        if (first_real == kNil) {
            return remaining;  // only the dummy remains
        }

        PooledOrder& maker = pool_[first_real];
        if (maker.cancelled.load(kAcq) || maker.quantity.load(kAcq) == 0) {
            // exhausted/cancelled: advance the dummy past it
            level.head.compare_exchange_strong(head_slot, first_real, kAcqRel, kAcq);
            continue;
        }

        uint32_t claimed = claimQuantity(maker.quantity, remaining);
        if (claimed == 0) {
            continue;  // raced with another thread claiming the rest and retry
        }
        trades.push_back(Trade{taker_id, maker.order_id, maker.price, claimed});
        remaining -= claimed;
        level.total_quantity.fetch_sub(claimed, kAcqRel);
        if (maker.quantity.load(kAcq) == 0) {
            // fully filled, clear so a later stray Cancel/Modify can't resurrect it
            id_to_slot_[maker.order_id].store(kNil, kRel);
        }
    }
}

void LockFreeOrderBook::rest(Side side, int64_t price, uint64_t order_id, uint32_t quantity, uint64_t timestamp_ns) {
    int64_t idx = price - min_price_;
    uint32_t slot = allocateSlot();
    PooledOrder& order = pool_[slot];
    order.order_id = order_id;
    order.price = price;
    order.quantity.store(quantity, kRelaxed);
    order.timestamp_ns = timestamp_ns;
    order.side = side;
    order.cancelled.store(false, kRelaxed);
    order.next.store(kNil, kRelaxed);
    id_to_slot_[order_id].store(slot, kRel);

    PriceLevel& level = (side == Side::Buy) ? bid_levels_[idx] : ask_levels_[idx];
    pushBack(level, slot);
    level.total_quantity.fetch_add(quantity, kAcqRel);

    std::lock_guard<std::mutex> lock(best_mutex_);
    if (side == Side::Buy) {
        if (best_bid_idx_ == -1 || idx > best_bid_idx_) {
            best_bid_idx_ = idx;
        }
    } else {
        if (best_ask_idx_ == -1 || idx < best_ask_idx_) {
            best_ask_idx_ = idx;
        }
    }
}

void LockFreeOrderBook::advanceBestAsk(int64_t from_idx) {
    int64_t next = from_idx + 1;
    while (next < static_cast<int64_t>(price_range_) &&
           ask_levels_[next].total_quantity.load(kAcq) == 0) {
        ++next;
    }
    best_ask_idx_ = (next < static_cast<int64_t>(price_range_)) ? next : -1;
}

void LockFreeOrderBook::advanceBestBid(int64_t from_idx) {
    int64_t next = from_idx - 1;
    while (next >= 0 && bid_levels_[next].total_quantity.load(kAcq) == 0) {
        --next;
    }
    best_bid_idx_ = next;
}

std::vector<Trade> LockFreeOrderBook::addOrder(uint64_t order_id, Side side, int64_t price, uint32_t quantity, uint64_t timestamp_ns) {
    int64_t idx = price - min_price_;
    if (idx < 0 || idx >= static_cast<int64_t>(price_range_)) {
        throw std::out_of_range("LockFreeOrderBook: price outside configured range");
    }
    if (order_id >= id_to_slot_.size()) {
        throw std::out_of_range("LockFreeOrderBook: order_id outside configured capacity");
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
            remaining = matchLevel(ask_levels_[ask_idx], order_id, remaining, trades);
            if (ask_levels_[ask_idx].total_quantity.load(kAcq) == 0) {
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
            remaining = matchLevel(bid_levels_[bid_idx], order_id, remaining, trades);
            if (bid_levels_[bid_idx].total_quantity.load(kAcq) == 0) {
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

bool LockFreeOrderBook::cancelOrder(uint64_t order_id) {
    if (order_id >= id_to_slot_.size()) {
        return false;
    }
    uint32_t slot = id_to_slot_[order_id].load(kAcq);
    if (slot == kNil) {
        return false;
    }

    bool expected = false;
    if (!pool_[slot].cancelled.compare_exchange_strong(expected, true, kAcqRel, kAcq)) {
        return false;  // already cancelled or racing with another cancel
    }
    id_to_slot_[order_id].store(kNil, kRel);

    Side side = pool_[slot].side;
    int64_t idx = pool_[slot].price - min_price_;
    uint32_t reclaimed = pool_[slot].quantity.exchange(0, kAcqRel);

    PriceLevel& level = (side == Side::Buy) ? bid_levels_[idx] : ask_levels_[idx];
    level.total_quantity.fetch_sub(reclaimed, kAcqRel);

    if (level.total_quantity.load(kAcq) == 0) {
        std::lock_guard<std::mutex> lock(best_mutex_);
        if (side == Side::Buy && best_bid_idx_ == idx) {
            advanceBestBid(idx);
        } else if (side == Side::Sell && best_ask_idx_ == idx) {
            advanceBestAsk(idx);
        }
    }
    return true;
}

std::vector<Trade> LockFreeOrderBook::modifyOrder(uint64_t order_id, int64_t new_price, uint32_t new_quantity, uint64_t timestamp_ns) {
    if (order_id >= id_to_slot_.size() || id_to_slot_[order_id].load(kAcq) == kNil) {
        return {};
    }
    Side side = pool_[id_to_slot_[order_id].load(kAcq)].side;
    cancelOrder(order_id);
    return addOrder(order_id, side, new_price, new_quantity, timestamp_ns);
}

std::vector<Trade> LockFreeOrderBook::apply(const OrderEvent& event) {
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

std::optional<int64_t> LockFreeOrderBook::bestBid() const {
    std::lock_guard<std::mutex> lock(best_mutex_);
    if (best_bid_idx_ == -1) {
        return std::nullopt;
    }
    return min_price_ + best_bid_idx_;
}

std::optional<int64_t> LockFreeOrderBook::bestAsk() const {
    std::lock_guard<std::mutex> lock(best_mutex_);
    if (best_ask_idx_ == -1) {
        return std::nullopt;
    }
    return min_price_ + best_ask_idx_;
}

uint32_t LockFreeOrderBook::quantityAt(Side side, int64_t price) const {
    int64_t idx = price - min_price_;
    if (idx < 0 || idx >= static_cast<int64_t>(price_range_)) {
        return 0;
    }
    const PriceLevel& level = (side == Side::Buy) ? bid_levels_[idx] : ask_levels_[idx];
    return static_cast<uint32_t>(level.total_quantity.load(kAcq));
}

} // namespace orderbook
