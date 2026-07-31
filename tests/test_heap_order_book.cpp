#include "orderbook/heap_order_book.h"
#include "test_framework.h"

using namespace orderbook;

TEST(heap_resting_order_with_no_cross_has_no_trades) {
    HeapOrderBook book;
    auto trades = book.addOrder(1, Side::Buy, 100, 10, 0);
    CHECK(trades.empty());
    CHECK_EQ(*book.bestBid(), 100);
    CHECK(!book.bestAsk().has_value());
}

TEST(heap_crossing_order_fully_fills_resting_order) {
    HeapOrderBook book;
    book.addOrder(1, Side::Sell, 100, 10, 0);
    auto trades = book.addOrder(2, Side::Buy, 100, 10, 1);
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].maker_order_id, 1u);
    CHECK_EQ(trades[0].taker_order_id, 2u);
    CHECK_EQ(trades[0].quantity, 10u);
    CHECK(!book.bestAsk().has_value());
}

TEST(heap_crossing_order_partially_fills_and_rests_remainder) {
    HeapOrderBook book;
    book.addOrder(1, Side::Sell, 100, 10, 0);
    auto trades = book.addOrder(2, Side::Buy, 100, 15, 1);
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].quantity, 10u);
    CHECK_EQ(*book.bestBid(), 100);
    CHECK_EQ(book.quantityAt(Side::Buy, 100), 5u);
}

TEST(heap_fifo_priority_within_price_level) {
    HeapOrderBook book;
    book.addOrder(1, Side::Sell, 100, 5, 0);
    book.addOrder(2, Side::Sell, 100, 5, 1);
    auto trades = book.addOrder(3, Side::Buy, 100, 5, 2);
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].maker_order_id, 1u);  // order 1 was resting first
    CHECK_EQ(book.quantityAt(Side::Sell, 100), 5u);
}

TEST(heap_cancel_removes_resting_order) {
    HeapOrderBook book;
    book.addOrder(1, Side::Buy, 100, 10, 0);
    CHECK(book.cancelOrder(1));
    CHECK(!book.bestBid().has_value());
    CHECK(!book.cancelOrder(1));  // already gone
}

TEST(heap_modify_reprices_and_can_cross) {
    HeapOrderBook book;
    book.addOrder(1, Side::Sell, 105, 10, 0);
    book.addOrder(2, Side::Buy, 100, 10, 1);
    auto trades = book.modifyOrder(2, 105, 10, 2);  // raise bid to cross the ask
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].price, 105);
}

TEST(heap_apply_dispatches_by_event_type) {
    HeapOrderBook book;
    book.apply(OrderEvent{0, 1, EventType::Add, Side::Buy, 100, 10});
    CHECK_EQ(*book.bestBid(), 100);
    book.apply(OrderEvent{1, 1, EventType::Cancel, Side::Buy, 100, 10});
    CHECK(!book.bestBid().has_value());
}

TEST(heap_best_bid_skips_to_next_occupied_level_after_drain) {
    HeapOrderBook book;
    book.addOrder(1, Side::Buy, 100, 10, 0);
    book.addOrder(2, Side::Buy, 90, 10, 1);
    book.cancelOrder(1);
    CHECK_EQ(*book.bestBid(), 90);
}

TEST(heap_best_ask_skips_to_next_occupied_level_after_drain) {
    HeapOrderBook book;
    book.addOrder(1, Side::Sell, 100, 10, 0);
    book.addOrder(2, Side::Sell, 110, 10, 1);
    book.cancelOrder(1);
    CHECK_EQ(*book.bestAsk(), 110);
}

// stale heap entries from repeated drains shouldn't confuse bestBid later
TEST(heap_survives_repeated_cancel_and_refill_at_same_price) {
    HeapOrderBook book;
    for (int i = 0; i < 5; ++i) {
        uint64_t id = static_cast<uint64_t>(100 + i);
        book.addOrder(id, Side::Buy, 100, 10, 0);
        book.cancelOrder(id);
    }
    CHECK(!book.bestBid().has_value());

    book.addOrder(999, Side::Buy, 100, 10, 0);
    CHECK_EQ(*book.bestBid(), 100);
    CHECK_EQ(book.quantityAt(Side::Buy, 100), 10u);
}

// No fixed price range or preallocated capacity, unlike FlatOrderBook
TEST(heap_handles_arbitrary_wide_price_range) {
    HeapOrderBook book;
    book.addOrder(1, Side::Buy, -1'000'000, 10, 0);
    book.addOrder(2, Side::Buy, 5'000'000'000LL, 10, 1);
    CHECK_EQ(*book.bestBid(), 5'000'000'000LL);
}