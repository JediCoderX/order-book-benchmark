#include <thread>
#include <vector>

#include "orderbook/fine_lock_order_book.h"
#include "test_framework.h"

using namespace orderbook;

namespace {
FineLockOrderBook makeBook() { return FineLockOrderBook(0, 1000, 100, 100); }
} // namespace

TEST(fine_lock_resting_order_with_no_cross_has_no_trades) {
    auto book = makeBook();
    auto trades = book.addOrder(1, Side::Buy, 100, 10, 0);
    CHECK(trades.empty());
    CHECK_EQ(*book.bestBid(), 100);
}

TEST(fine_lock_crossing_order_fully_fills_resting_order) {
    auto book = makeBook();
    book.addOrder(1, Side::Sell, 100, 10, 0);
    auto trades = book.addOrder(2, Side::Buy, 100, 10, 1);
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].maker_order_id, 1u);
    CHECK_EQ(trades[0].quantity, 10u);
}

TEST(fine_lock_crossing_order_partially_fills_and_rests_remainder) {
    auto book = makeBook();
    book.addOrder(1, Side::Sell, 100, 10, 0);
    auto trades = book.addOrder(2, Side::Buy, 100, 15, 1);
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].quantity, 10u);
    CHECK_EQ(book.quantityAt(Side::Buy, 100), 5u);
}

TEST(fine_lock_fifo_priority_within_price_level) {
    auto book = makeBook();
    book.addOrder(1, Side::Sell, 100, 5, 0);
    book.addOrder(2, Side::Sell, 100, 5, 1);
    auto trades = book.addOrder(3, Side::Buy, 100, 5, 2);
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].maker_order_id, 1u);
}

TEST(fine_lock_cancel_removes_resting_order) {
    auto book = makeBook();
    book.addOrder(1, Side::Buy, 100, 10, 0);
    CHECK(book.cancelOrder(1));
    CHECK(!book.bestBid().has_value());
}

TEST(fine_lock_modify_reprices_and_can_cross) {
    auto book = makeBook();
    book.addOrder(1, Side::Sell, 105, 10, 0);
    book.addOrder(2, Side::Buy, 100, 10, 1);
    auto trades = book.modifyOrder(2, 105, 10, 2);
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].price, 105);
}

TEST(fine_lock_best_bid_skips_to_next_occupied_level_after_drain) {
    auto book = makeBook();
    book.addOrder(1, Side::Buy, 100, 10, 0);
    book.addOrder(2, Side::Buy, 90, 10, 1);
    book.cancelOrder(1);
    CHECK_EQ(*book.bestBid(), 90);
}

TEST(fine_lock_best_ask_skips_to_next_occupied_level_after_drain) {
    auto book = makeBook();
    book.addOrder(1, Side::Sell, 100, 10, 0);
    book.addOrder(2, Side::Sell, 110, 10, 1);
    book.cancelOrder(1);
    CHECK_EQ(*book.bestAsk(), 110);
}

// Each thread owns a disjoint price lane and id range, so its own orders never
// cross with another thread's, but all threads genuinely contend on the
// shared best-index tracking and the shared pool allocator
TEST(fine_lock_survives_concurrent_access_from_multiple_threads) {
    constexpr int kThreads = 8;
    constexpr uint64_t kOrdersPerThread = 500;
    constexpr uint32_t kQuantity = 10;

    FineLockOrderBook book(0, 1000, kThreads * kOrdersPerThread + 10,
                            kThreads * kOrdersPerThread + 10);

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&book, t]() {
            int64_t price = 100 + t;
            uint64_t base_id = static_cast<uint64_t>(t) * kOrdersPerThread;
            for (uint64_t i = 0; i < kOrdersPerThread; ++i) {
                uint64_t id = base_id + i;
                book.addOrder(id, Side::Buy, price, kQuantity, 0);
                if (i % 2 == 0) {
                    book.cancelOrder(id);
                }
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    uint32_t expected_per_lane = static_cast<uint32_t>((kOrdersPerThread / 2) * kQuantity);
    int64_t best_occupied_price = -1;
    for (int t = 0; t < kThreads; ++t) {
        CHECK_EQ(book.quantityAt(Side::Buy, 100 + t), expected_per_lane);
        if (100 + t > best_occupied_price) {
            best_occupied_price = 100 + t;
        }
    }
    CHECK_EQ(*book.bestBid(), best_occupied_price);
}