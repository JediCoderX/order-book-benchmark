#include <stdexcept>

#include "orderbook/flat_order_book.h"
#include "test_framework.h"

using namespace orderbook;

namespace {
// min_price=0, price_range=1000, pool_capacity=100, max_order_id=100
FlatOrderBook makeBook() { return FlatOrderBook(0, 1000, 100, 100); }
} // namespace

TEST(flat_resting_order_with_no_cross_has_no_trades) {
    auto book = makeBook();
    auto trades = book.addOrder(1, Side::Buy, 100, 10, 0);
    CHECK(trades.empty());
    CHECK_EQ(*book.bestBid(), 100);
    CHECK(!book.bestAsk().has_value());
}

TEST(flat_crossing_order_fully_fills_resting_order) {
    auto book = makeBook();
    book.addOrder(1, Side::Sell, 100, 10, 0);
    auto trades = book.addOrder(2, Side::Buy, 100, 10, 1);
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].maker_order_id, 1u);
    CHECK_EQ(trades[0].taker_order_id, 2u);
    CHECK_EQ(trades[0].quantity, 10u);
    CHECK(!book.bestAsk().has_value());
}

TEST(flat_crossing_order_partially_fills_and_rests_remainder) {
    auto book = makeBook();
    book.addOrder(1, Side::Sell, 100, 10, 0);
    auto trades = book.addOrder(2, Side::Buy, 100, 15, 1);
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].quantity, 10u);
    CHECK_EQ(*book.bestBid(), 100);
    CHECK_EQ(book.quantityAt(Side::Buy, 100), 5u);
}

TEST(flat_fifo_priority_within_price_level) {
    auto book = makeBook();
    book.addOrder(1, Side::Sell, 100, 5, 0);
    book.addOrder(2, Side::Sell, 100, 5, 1);
    auto trades = book.addOrder(3, Side::Buy, 100, 5, 2);
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].maker_order_id, 1u);  // order 1 was resting first
    CHECK_EQ(book.quantityAt(Side::Sell, 100), 5u);
}

TEST(flat_cancel_removes_resting_order) {
    auto book = makeBook();
    book.addOrder(1, Side::Buy, 100, 10, 0);
    CHECK(book.cancelOrder(1));
    CHECK(!book.bestBid().has_value());
    CHECK(!book.cancelOrder(1));  // already gone
}

TEST(flat_modify_reprices_and_can_cross) {
    auto book = makeBook();
    book.addOrder(1, Side::Sell, 105, 10, 0);
    book.addOrder(2, Side::Buy, 100, 10, 1);
    auto trades = book.modifyOrder(2, 105, 10, 2);  // raise bid to cross the ask
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].price, 105);
}

TEST(flat_apply_dispatches_by_event_type) {
    auto book = makeBook();
    book.apply(OrderEvent{0, 1, EventType::Add, Side::Buy, 100, 10});
    CHECK_EQ(*book.bestBid(), 100);
    book.apply(OrderEvent{1, 1, EventType::Cancel, Side::Buy, 100, 10});
    CHECK(!book.bestBid().has_value());
}

TEST(flat_best_bid_skips_to_next_occupied_level_after_drain) {
    auto book = makeBook();
    book.addOrder(1, Side::Buy, 100, 10, 0);
    book.addOrder(2, Side::Buy, 90, 10, 1);
    book.cancelOrder(1);
    CHECK_EQ(*book.bestBid(), 90);
}

TEST(flat_best_ask_skips_to_next_occupied_level_after_drain) {
    auto book = makeBook();
    book.addOrder(1, Side::Sell, 100, 10, 0);
    book.addOrder(2, Side::Sell, 110, 10, 1);
    book.cancelOrder(1);
    CHECK_EQ(*book.bestAsk(), 110);
}

TEST(flat_out_of_range_price_throws) {
    auto book = makeBook();
    bool threw = false;
    try {
        book.addOrder(1, Side::Buy, 5000, 10, 0);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    CHECK(threw);
}