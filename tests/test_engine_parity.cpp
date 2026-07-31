#include "orderbook/coarse_lock_order_book.h"
#include "orderbook/fine_lock_order_book.h"
#include "orderbook/flat_order_book.h"
#include "orderbook/feed_parser.h"
#include "orderbook/heap_order_book.h"
#include "orderbook/lock_free_order_book.h"
#include "orderbook/tree_order_book.h"
#include "test_framework.h"

using namespace orderbook;

// Replays the same feed through both engines, checking every trade and
// best bid/ask match exactly.
TEST(flat_order_book_matches_baseline_on_replayed_feed) {
    TreeOrderBook baseline;
    FlatOrderBook flat(/*min_price=*/0, /*price_range=*/50000, /*pool_capacity=*/10000,
                        /*max_order_id=*/20000);

    FeedParser parser("data/random_feed.csv");
    OrderEvent event{};
    uint64_t events_checked = 0;

    while (parser.next(event)) {
        auto expected = baseline.apply(event);
        auto actual = flat.apply(event);

        CHECK_EQ(actual.size(), expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            CHECK_EQ(actual[i].taker_order_id, expected[i].taker_order_id);
            CHECK_EQ(actual[i].maker_order_id, expected[i].maker_order_id);
            CHECK_EQ(actual[i].price, expected[i].price);
            CHECK_EQ(actual[i].quantity, expected[i].quantity);
        }

        CHECK(flat.bestBid() == baseline.bestBid());
        CHECK(flat.bestAsk() == baseline.bestAsk());
        ++events_checked;
    }

    CHECK_EQ(events_checked, 10000u);
}

TEST(heap_order_book_matches_baseline_on_replayed_feed) {
    TreeOrderBook baseline;
    HeapOrderBook heap;

    FeedParser parser("data/random_feed.csv");
    OrderEvent event{};
    uint64_t events_checked = 0;

    while (parser.next(event)) {
        auto expected = baseline.apply(event);
        auto actual = heap.apply(event);

        CHECK_EQ(actual.size(), expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            CHECK_EQ(actual[i].taker_order_id, expected[i].taker_order_id);
            CHECK_EQ(actual[i].maker_order_id, expected[i].maker_order_id);
            CHECK_EQ(actual[i].price, expected[i].price);
            CHECK_EQ(actual[i].quantity, expected[i].quantity);
        }

        CHECK(heap.bestBid() == baseline.bestBid());
        CHECK(heap.bestAsk() == baseline.bestAsk());
        ++events_checked;
    }

    CHECK_EQ(events_checked, 10000u);
}

// Single-threaded here on purpose: proves the mutex wrapper doesn't change
// behavior at all when there's no real concurrency, before it gets used
// concurrently elsewhere.
TEST(coarse_lock_order_book_matches_baseline_on_replayed_feed) {
    TreeOrderBook baseline;
    CoarseLockOrderBook locked(/*min_price=*/0, /*price_range=*/50000, /*pool_capacity=*/10000,
                                /*max_order_id=*/20000);

    FeedParser parser("data/random_feed.csv");
    OrderEvent event{};
    uint64_t events_checked = 0;

    while (parser.next(event)) {
        auto expected = baseline.apply(event);
        auto actual = locked.apply(event);

        CHECK_EQ(actual.size(), expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            CHECK_EQ(actual[i].taker_order_id, expected[i].taker_order_id);
            CHECK_EQ(actual[i].maker_order_id, expected[i].maker_order_id);
            CHECK_EQ(actual[i].price, expected[i].price);
            CHECK_EQ(actual[i].quantity, expected[i].quantity);
        }

        CHECK(locked.bestBid() == baseline.bestBid());
        CHECK(locked.bestAsk() == baseline.bestAsk());
        ++events_checked;
    }

    CHECK_EQ(events_checked, 10000u);
}

// Single-threaded here too, for the same reason: proves the fine-grained
// locking doesn't change behavior before it's ever used concurrently.
TEST(fine_lock_order_book_matches_baseline_on_replayed_feed) {
    TreeOrderBook baseline;
    FineLockOrderBook fine(/*min_price=*/0, /*price_range=*/50000, /*pool_capacity=*/10000,
                            /*max_order_id=*/20000);

    FeedParser parser("data/random_feed.csv");
    OrderEvent event{};
    uint64_t events_checked = 0;

    while (parser.next(event)) {
        auto expected = baseline.apply(event);
        auto actual = fine.apply(event);

        CHECK_EQ(actual.size(), expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            CHECK_EQ(actual[i].taker_order_id, expected[i].taker_order_id);
            CHECK_EQ(actual[i].maker_order_id, expected[i].maker_order_id);
            CHECK_EQ(actual[i].price, expected[i].price);
            CHECK_EQ(actual[i].quantity, expected[i].quantity);
        }

        CHECK(fine.bestBid() == baseline.bestBid());
        CHECK(fine.bestAsk() == baseline.bestAsk());
        ++events_checked;
    }

    CHECK_EQ(events_checked, 10000u);
}

// Single-threaded here
TEST(lock_free_order_book_matches_baseline_on_replayed_feed) {
    TreeOrderBook baseline;
    LockFreeOrderBook lockfree(/*min_price=*/0, /*price_range=*/50000,
                                /*pool_capacity=*/15000, /*max_order_id=*/20000);

    FeedParser parser("data/random_feed.csv");
    OrderEvent event{};
    uint64_t events_checked = 0;

    while (parser.next(event)) {
        auto expected = baseline.apply(event);
        auto actual = lockfree.apply(event);

        CHECK_EQ(actual.size(), expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            CHECK_EQ(actual[i].taker_order_id, expected[i].taker_order_id);
            CHECK_EQ(actual[i].maker_order_id, expected[i].maker_order_id);
            CHECK_EQ(actual[i].price, expected[i].price);
            CHECK_EQ(actual[i].quantity, expected[i].quantity);
        }

        CHECK(lockfree.bestBid() == baseline.bestBid());
        CHECK(lockfree.bestAsk() == baseline.bestAsk());
        ++events_checked;
    }

    CHECK_EQ(events_checked, 10000u);
}