#include "orderbook/heap_order_book.h"

#include <algorithm>

namespace orderbook {

void HeapOrderBook::cleanBidTop() {
    while (!bid_heap_.empty() && !bid_levels_.count(bid_heap_.top())) {
        bid_heap_.pop();
    }
}

void HeapOrderBook::cleanAskTop() {
    while (!ask_heap_.empty() && !ask_levels_.count(ask_heap_.top())) {
        ask_heap_.pop();
    }
}

void HeapOrderBook::rest(Side side, int64_t price, uint64_t order_id, uint32_t quantity,
                          uint64_t timestamp_ns) {
    if (side == Side::Buy) {
        bool is_new_level = !bid_levels_.count(price);
        Level& level = bid_levels_[price];
        level.push_back(RestingOrder{order_id, side, price, quantity, timestamp_ns});
        order_index_[order_id] = OrderLocation{side, price, std::prev(level.end())};
        if (is_new_level) {
            bid_heap_.push(price);
        }
    } else {
        bool is_new_level = !ask_levels_.count(price);
        Level& level = ask_levels_[price];
        level.push_back(RestingOrder{order_id, side, price, quantity, timestamp_ns});
        order_index_[order_id] = OrderLocation{side, price, std::prev(level.end())};
        if (is_new_level) {
            ask_heap_.push(price);
        }
    }
}

std::vector<Trade> HeapOrderBook::addOrder(uint64_t order_id, Side side, int64_t price,
                                            uint32_t quantity, uint64_t timestamp_ns) {
    std::vector<Trade> trades;
    uint32_t remaining = quantity;

    if (side == Side::Buy) {
        while (remaining > 0) {
            cleanAskTop();
            if (ask_heap_.empty() || ask_heap_.top() > price) {
                break;
            }
            int64_t level_price = ask_heap_.top();
            Level& level = ask_levels_.at(level_price);
            while (remaining > 0 && !level.empty()) {
                RestingOrder& maker = level.front();
                uint32_t traded = std::min(remaining, maker.quantity);
                trades.push_back(Trade{order_id, maker.order_id, level_price, traded});
                remaining -= traded;
                maker.quantity -= traded;
                if (maker.quantity == 0) {
                    order_index_.erase(maker.order_id);
                    level.pop_front();
                }
            }
            if (level.empty()) {
                ask_levels_.erase(level_price);
                ask_heap_.pop();
            }
        }
    } else {
        while (remaining > 0) {
            cleanBidTop();
            if (bid_heap_.empty() || bid_heap_.top() < price) {
                break;
            }
            int64_t level_price = bid_heap_.top();
            Level& level = bid_levels_.at(level_price);
            while (remaining > 0 && !level.empty()) {
                RestingOrder& maker = level.front();
                uint32_t traded = std::min(remaining, maker.quantity);
                trades.push_back(Trade{order_id, maker.order_id, level_price, traded});
                remaining -= traded;
                maker.quantity -= traded;
                if (maker.quantity == 0) {
                    order_index_.erase(maker.order_id);
                    level.pop_front();
                }
            }
            if (level.empty()) {
                bid_levels_.erase(level_price);
                bid_heap_.pop();
            }
        }
    }

    if (remaining > 0) {
        rest(side, price, order_id, remaining, timestamp_ns);
    }
    return trades;
}

bool HeapOrderBook::cancelOrder(uint64_t order_id) {
    auto found = order_index_.find(order_id);
    if (found == order_index_.end()) {
        return false;
    }

    const OrderLocation loc = found->second;
    if (loc.side == Side::Buy) {
        Level& level = bid_levels_.at(loc.price);
        level.erase(loc.iterator);
        if (level.empty()) {
            bid_levels_.erase(loc.price);
        }
    } else {
        Level& level = ask_levels_.at(loc.price);
        level.erase(loc.iterator);
        if (level.empty()) {
            ask_levels_.erase(loc.price);
        }
    }

    order_index_.erase(found);
    return true;
}

std::vector<Trade> HeapOrderBook::modifyOrder(uint64_t order_id, int64_t new_price,
                                               uint32_t new_quantity, uint64_t timestamp_ns) {
    auto found = order_index_.find(order_id);
    if (found == order_index_.end()) {
        return {};
    }
    Side side = found->second.side;
    cancelOrder(order_id);
    return addOrder(order_id, side, new_price, new_quantity, timestamp_ns);
}

std::vector<Trade> HeapOrderBook::apply(const OrderEvent& event) {
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

std::optional<int64_t> HeapOrderBook::bestBid() {
    cleanBidTop();
    if (bid_heap_.empty()) {
        return std::nullopt;
    }
    return bid_heap_.top();
}

std::optional<int64_t> HeapOrderBook::bestAsk() {
    cleanAskTop();
    if (ask_heap_.empty()) {
        return std::nullopt;
    }
    return ask_heap_.top();
}

uint32_t HeapOrderBook::quantityAt(Side side, int64_t price) const {
    const auto& levels = (side == Side::Buy) ? bid_levels_ : ask_levels_;
    auto it = levels.find(price);
    if (it == levels.end()) {
        return 0;
    }
    uint32_t total = 0;
    for (const auto& order : it->second) {
        total += order.quantity;
    }
    return total;
}

} // namespace orderbook