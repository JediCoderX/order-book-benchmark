#include "orderbook/order_book.h"
#include "test_framework.h"

using namespace orderbook;

TEST(resting_order_with_no_cross_has_no_trades) {
    OrderBook book;
    auto trades = book.addOrder(1, Side::Buy, 100, 10, 0);
    CHECK(trades.empty());
    CHECK_EQ(*book.bestBid(), 100);
    CHECK(!book.bestAsk().has_value());
}

TEST(crossing_order_fully_fills_resting_order) {
    OrderBook book;
    book.addOrder(1, Side::Sell, 100, 10, 0);
    auto trades = book.addOrder(2, Side::Buy, 100, 10, 1);
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].maker_order_id, 1u);
    CHECK_EQ(trades[0].taker_order_id, 2u);
    CHECK_EQ(trades[0].quantity, 10u);
    CHECK(!book.bestAsk().has_value());
}

TEST(crossing_order_partially_fills_and_rests_remainder) {
    OrderBook book;
    book.addOrder(1, Side::Sell, 100, 10, 0);
    auto trades = book.addOrder(2, Side::Buy, 100, 15, 1);
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].quantity, 10u);
    CHECK_EQ(*book.bestBid(), 100);
    CHECK_EQ(book.quantityAt(Side::Buy, 100), 5u);
}

TEST(fifo_priority_within_price_level) {
    OrderBook book;
    book.addOrder(1, Side::Sell, 100, 5, 0);
    book.addOrder(2, Side::Sell, 100, 5, 1);
    auto trades = book.addOrder(3, Side::Buy, 100, 5, 2);
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].maker_order_id, 1u);  // order 1 was resting first
    CHECK_EQ(book.quantityAt(Side::Sell, 100), 5u);
}

TEST(cancel_removes_resting_order) {
    OrderBook book;
    book.addOrder(1, Side::Buy, 100, 10, 0);
    CHECK(book.cancelOrder(1));
    CHECK(!book.bestBid().has_value());
    CHECK(!book.cancelOrder(1));  // already gone
}

TEST(modify_reprices_and_can_cross) {
    OrderBook book;
    book.addOrder(1, Side::Sell, 105, 10, 0);
    book.addOrder(2, Side::Buy, 100, 10, 1);
    auto trades = book.modifyOrder(2, 105, 10, 2);  // raise bid to cross the ask
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].price, 105);
}

TEST(apply_dispatches_by_event_type) {
    OrderBook book;
    book.apply(OrderEvent{0, 1, EventType::Add, Side::Buy, 100, 10});
    CHECK_EQ(*book.bestBid(), 100);
    book.apply(OrderEvent{1, 1, EventType::Cancel, Side::Buy, 100, 10});
    CHECK(!book.bestBid().has_value());
}