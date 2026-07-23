#include "orderbook/flat_order_book.h"

#include <algorithm>
#include <stdexcept>

namespace orderbook {

FlatOrderBook::FlatOrderBook(int64_t min_price, size_t price_range, size_t pool_capacity,
                              uint64_t max_order_id)
    : min_price_(min_price),
      price_range_(price_range),
      bid_levels_(price_range),
      ask_levels_(price_range),
      pool_(pool_capacity),
      id_to_slot_(max_order_id, kNil) {
    for (size_t i = 0; i < pool_capacity; ++i) {
        pool_[i].next = (i + 1 < pool_capacity) ? static_cast<uint32_t>(i + 1) : kNil;
    }
    free_head_ = pool_capacity > 0 ? 0 : kNil;
}

uint32_t FlatOrderBook::allocateSlot() {
    if (free_head_ == kNil) {
        throw std::runtime_error("FlatOrderBook: order pool exhausted");
    }
    uint32_t slot = free_head_;
    free_head_ = pool_[slot].next;
    return slot;
}

void FlatOrderBook::freeSlot(uint32_t slot) {
    pool_[slot].next = free_head_;
    free_head_ = slot;
}

void FlatOrderBook::pushBack(PriceLevel& level, uint32_t slot) {
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

void FlatOrderBook::unlink(PriceLevel& level, uint32_t slot) {
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

void FlatOrderBook::rest(Side side, int64_t price, uint64_t order_id, uint32_t quantity,
                          uint64_t timestamp_ns) {
    int64_t idx = price - min_price_;
    uint32_t slot = allocateSlot();
    pool_[slot] = PooledOrder{order_id, price, quantity, timestamp_ns, side, kNil, kNil};
    id_to_slot_[order_id] = slot;

    if (side == Side::Buy) {
        PriceLevel& level = bid_levels_[idx];
        pushBack(level, slot);
        level.total_quantity += quantity;
        if (best_bid_idx_ == -1 || idx > best_bid_idx_) {
            best_bid_idx_ = idx;
        }
    } else {
        PriceLevel& level = ask_levels_[idx];
        pushBack(level, slot);
        level.total_quantity += quantity;
        if (best_ask_idx_ == -1 || idx < best_ask_idx_) {
            best_ask_idx_ = idx;
        }
    }
}

std::vector<Trade> FlatOrderBook::addOrder(uint64_t order_id, Side side, int64_t price,
                                            uint32_t quantity, uint64_t timestamp_ns) {
    int64_t idx = price - min_price_;
    if (idx < 0 || idx >= static_cast<int64_t>(price_range_)) {
        throw std::out_of_range("FlatOrderBook: price outside configured range");
    }
    if (order_id >= id_to_slot_.size()) {
        throw std::out_of_range("FlatOrderBook: order_id outside configured capacity");
    }

    std::vector<Trade> trades;
    uint32_t remaining = quantity;

    if (side == Side::Buy) {
        while (remaining > 0 && best_ask_idx_ != -1 && best_ask_idx_ <= idx) {
            PriceLevel& level = ask_levels_[best_ask_idx_];
            while (remaining > 0 && level.head != kNil) {
                PooledOrder& maker = pool_[level.head];
                uint32_t traded = std::min(remaining, maker.quantity);
                trades.push_back(Trade{order_id, maker.order_id, maker.price, traded});
                remaining -= traded;
                maker.quantity -= traded;
                level.total_quantity -= traded;
                if (maker.quantity == 0) {
                    uint32_t filled = level.head;
                    unlink(level, filled);
                    id_to_slot_[maker.order_id] = kNil;
                    freeSlot(filled);
                }
            }
            if (level.head == kNil) {
                int64_t next = best_ask_idx_ + 1;
                while (next < static_cast<int64_t>(price_range_) && ask_levels_[next].head == kNil) {
                    ++next;
                }
                best_ask_idx_ = (next < static_cast<int64_t>(price_range_)) ? next : -1;
            }
        }
    } else {
        while (remaining > 0 && best_bid_idx_ != -1 && best_bid_idx_ >= idx) {
            PriceLevel& level = bid_levels_[best_bid_idx_];
            while (remaining > 0 && level.head != kNil) {
                PooledOrder& maker = pool_[level.head];
                uint32_t traded = std::min(remaining, maker.quantity);
                trades.push_back(Trade{order_id, maker.order_id, maker.price, traded});
                remaining -= traded;
                maker.quantity -= traded;
                level.total_quantity -= traded;
                if (maker.quantity == 0) {
                    uint32_t filled = level.head;
                    unlink(level, filled);
                    id_to_slot_[maker.order_id] = kNil;
                    freeSlot(filled);
                }
            }
            if (level.head == kNil) {
                int64_t next = best_bid_idx_ - 1;
                while (next >= 0 && bid_levels_[next].head == kNil) {
                    --next;
                }
                best_bid_idx_ = next;
            }
        }
    }

    if (remaining > 0) {
        rest(side, price, order_id, remaining, timestamp_ns);
    }
    return trades;
}

bool FlatOrderBook::cancelOrder(uint64_t order_id) {
    if (order_id >= id_to_slot_.size() || id_to_slot_[order_id] == kNil) {
        return false;
    }

    uint32_t slot = id_to_slot_[order_id];
    PooledOrder& order = pool_[slot];
    int64_t idx = order.price - min_price_;

    if (order.side == Side::Buy) {
        PriceLevel& level = bid_levels_[idx];
        level.total_quantity -= order.quantity;
        unlink(level, slot);
        if (level.head == kNil && idx == best_bid_idx_) {
            int64_t next = idx - 1;
            while (next >= 0 && bid_levels_[next].head == kNil) {
                --next;
            }
            best_bid_idx_ = next;
        }
    } else {
        PriceLevel& level = ask_levels_[idx];
        level.total_quantity -= order.quantity;
        unlink(level, slot);
        if (level.head == kNil && idx == best_ask_idx_) {
            int64_t next = idx + 1;
            while (next < static_cast<int64_t>(price_range_) && ask_levels_[next].head == kNil) {
                ++next;
            }
            best_ask_idx_ = (next < static_cast<int64_t>(price_range_)) ? next : -1;
        }
    }

    id_to_slot_[order_id] = kNil;
    freeSlot(slot);
    return true;
}

std::vector<Trade> FlatOrderBook::modifyOrder(uint64_t order_id, int64_t new_price,
                                               uint32_t new_quantity, uint64_t timestamp_ns) {
    if (order_id >= id_to_slot_.size() || id_to_slot_[order_id] == kNil) {
        return {};
    }
    Side side = pool_[id_to_slot_[order_id]].side;
    cancelOrder(order_id);
    return addOrder(order_id, side, new_price, new_quantity, timestamp_ns);
}

std::vector<Trade> FlatOrderBook::apply(const OrderEvent& event) {
    switch (event.type) {
        case EventType::Add:
            return addOrder(event.order_id, event.side, event.price, event.quantity,
                             event.timestamp_ns);
        case EventType::Cancel:
            cancelOrder(event.order_id);
            return {};
        case EventType::Modify:
            return modifyOrder(event.order_id, event.price, event.quantity, event.timestamp_ns);
    }
    return {};
}

std::optional<int64_t> FlatOrderBook::bestBid() const {
    if (best_bid_idx_ == -1) {
        return std::nullopt;
    }
    return min_price_ + best_bid_idx_;
}

std::optional<int64_t> FlatOrderBook::bestAsk() const {
    if (best_ask_idx_ == -1) {
        return std::nullopt;
    }
    return min_price_ + best_ask_idx_;
}

uint32_t FlatOrderBook::quantityAt(Side side, int64_t price) const {
    int64_t idx = price - min_price_;
    if (idx < 0 || idx >= static_cast<int64_t>(price_range_)) {
        return 0;
    }
    return static_cast<uint32_t>(side == Side::Buy ? bid_levels_[idx].total_quantity
                                                     : ask_levels_[idx].total_quantity);
}

} // namespace orderbook