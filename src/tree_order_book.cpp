#include "orderbook/tree_order_book.h"

#include <algorithm>

namespace orderbook {

template <typename BookMap, typename Crosses>
uint32_t TreeOrderBook::matchAgainst(BookMap& opposite, uint64_t taker_id, int64_t price,
                                  uint32_t remaining, Crosses crosses,
                                  std::vector<Trade>& trades) {
    while (remaining > 0 && !opposite.empty()) {
        auto level_it = opposite.begin();
        int64_t level_price = level_it->first;
        if (!crosses(level_price, price)) {
            break;
        }

        Level& level = level_it->second;
        while (remaining > 0 && !level.empty()) {
            RestingOrder& maker = level.front();
            uint32_t traded = std::min(remaining, maker.quantity);
            trades.push_back(Trade{taker_id, maker.order_id, level_price, traded});
            remaining -= traded;
            maker.quantity -= traded;
            if (maker.quantity == 0) {
                order_index_.erase(maker.order_id);
                level.pop_front();
            }
        }

        if (level.empty()) {
            opposite.erase(level_it);
        }
    }
    return remaining;
}

void TreeOrderBook::rest(Side side, int64_t price, uint64_t order_id, uint32_t quantity,
                      uint64_t timestamp_ns) {
    Level* level = (side == Side::Buy) ? &bids_[price] : &asks_[price];
    level->push_back(RestingOrder{order_id, side, price, quantity, timestamp_ns});
    order_index_[order_id] = OrderLocation{side, price, std::prev(level->end())};
}

std::vector<Trade> TreeOrderBook::addOrder(uint64_t order_id, Side side, int64_t price,
                                        uint32_t quantity, uint64_t timestamp_ns) {
    std::vector<Trade> trades;
    uint32_t remaining = quantity;

    if (side == Side::Buy) {
        remaining = matchAgainst(
            asks_, order_id, price, remaining,
            [](int64_t level_price, int64_t order_price) { return level_price <= order_price; },
            trades);
    } else {
        remaining = matchAgainst(
            bids_, order_id, price, remaining,
            [](int64_t level_price, int64_t order_price) { return level_price >= order_price; },
            trades);
    }

    if (remaining > 0) {
        rest(side, price, order_id, remaining, timestamp_ns);
    }
    return trades;
}

bool TreeOrderBook::cancelOrder(uint64_t order_id) {
    auto found = order_index_.find(order_id);
    if (found == order_index_.end()) {
        return false;
    }

    const OrderLocation loc = found->second;
    if (loc.side == Side::Buy) {
        auto level_it = bids_.find(loc.price);
        level_it->second.erase(loc.iterator);
        if (level_it->second.empty()) {
            bids_.erase(level_it);
        }
    } else {
        auto level_it = asks_.find(loc.price);
        level_it->second.erase(loc.iterator);
        if (level_it->second.empty()) {
            asks_.erase(level_it);
        }
    }

    order_index_.erase(found);
    return true;
}

std::vector<Trade> TreeOrderBook::modifyOrder(uint64_t order_id, int64_t new_price,
                                           uint32_t new_quantity, uint64_t timestamp_ns) {
    auto found = order_index_.find(order_id);
    if (found == order_index_.end()) {
        return {};
    }
    // cancel-replace: loses time priority, like most real venues
    Side side = found->second.side;
    cancelOrder(order_id);
    return addOrder(order_id, side, new_price, new_quantity, timestamp_ns);
}

std::vector<Trade> TreeOrderBook::apply(const OrderEvent& event) {
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

std::optional<int64_t> TreeOrderBook::bestBid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<int64_t> TreeOrderBook::bestAsk() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

uint32_t TreeOrderBook::quantityAt(Side side, int64_t price) const {
    uint32_t total = 0;
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it == bids_.end()) return 0;
        for (const auto& order : it->second) total += order.quantity;
    } else {
        auto it = asks_.find(price);
        if (it == asks_.end()) return 0;
        for (const auto& order : it->second) total += order.quantity;
    }
    return total;
}

} // namespace orderbook