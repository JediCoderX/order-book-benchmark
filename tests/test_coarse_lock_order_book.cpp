#include <thread>
#include <vector>

#include "orderbook/coarse_lock_order_book.h"
#include "test_framework.h"

using namespace orderbook;

namespace {
CoarseLockOrderBook makeBook() { return CoarseLockOrderBook(0, 1000, 100, 100); }
} // namespace

TEST(coarse_lock_resting_order_with_no_cross_has_no_trades) {
    auto book = makeBook();
    auto trades = book.addOrder(1, Side::Buy, 100, 10, 0);
    CHECK(trades.empty());
    CHECK_EQ(*book.bestBid(), 100);
}

TEST(coarse_lock_crossing_order_fully_fills_resting_order) {
    auto book = makeBook();
    book.addOrder(1, Side::Sell, 100, 10, 0);
    auto trades = book.addOrder(2, Side::Buy, 100, 10, 1);
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].maker_order_id, 1u);
    CHECK_EQ(trades[0].quantity, 10u);
}

TEST(coarse_lock_cancel_removes_resting_order) {
    auto book = makeBook();
    book.addOrder(1, Side::Buy, 100, 10, 0);
    CHECK(book.cancelOrder(1));
    CHECK(!book.bestBid().has_value());
}

TEST(coarse_lock_modify_reprices_and_can_cross) {
    auto book = makeBook();
    book.addOrder(1, Side::Sell, 105, 10, 0);
    book.addOrder(2, Side::Buy, 100, 10, 1);
    auto trades = book.modifyOrder(2, 105, 10, 2);
    CHECK_EQ(trades.size(), size_t{1});
    CHECK_EQ(trades[0].price, 105);
}

// Multiple threads mutate the SAME book at one
// Each thread uses its own disjoint price level and id range so its own orders never cross with another thread's
TEST(coarse_lock_survives_concurrent_access_from_multiple_threads) {
    constexpr int kThreads = 8;
    constexpr uint64_t kOrdersPerThread = 500;
    constexpr uint32_t kQuantity = 10;

    CoarseLockOrderBook book(0, 1000, kThreads * kOrdersPerThread + 10,
                              kThreads * kOrdersPerThread + 10);

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&book, t]() {
            int64_t price = 100 + t;  // each thread's own price lane
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

    // half of each thread's orders were cancelled, half still resting
    uint32_t expected_per_lane = static_cast<uint32_t>((kOrdersPerThread / 2) * kQuantity);
    for (int t = 0; t < kThreads; ++t) {
        CHECK_EQ(book.quantityAt(Side::Buy, 100 + t), expected_per_lane);
    }
}