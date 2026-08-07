#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "orderbook/coarse_lock_order_book.h"
#include "orderbook/feed_parser.h"
#include "orderbook/fine_lock_order_book.h"
#include "orderbook/flat_order_book.h"
#include "orderbook/heap_order_book.h"
#include "orderbook/lock_free_order_book.h"
#include "orderbook/tree_order_book.h"

using namespace orderbook;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int kTrials = 5;

struct Stats {
    double p50_ns = 0, p90_ns = 0, p99_ns = 0, p999_ns = 0, max_ns = 0;
    double throughput_ops_per_sec = 0;
};

double percentileOf(const std::vector<double>& sorted_ns, double p) {
    if (sorted_ns.empty()) {
        return 0.0;
    }
    size_t idx = static_cast<size_t>(p * (sorted_ns.size() - 1));
    return sorted_ns[idx];
}

Stats summarize(std::vector<double>& all_latencies_ns, std::vector<double>& throughputs) {
    std::sort(all_latencies_ns.begin(), all_latencies_ns.end());
    std::sort(throughputs.begin(), throughputs.end());

    Stats s;
    s.p50_ns = percentileOf(all_latencies_ns, 0.50);
    s.p90_ns = percentileOf(all_latencies_ns, 0.90);
    s.p99_ns = percentileOf(all_latencies_ns, 0.99);
    s.p999_ns = percentileOf(all_latencies_ns, 0.999);
    s.max_ns = all_latencies_ns.empty() ? 0.0 : all_latencies_ns.back();
    s.throughput_ops_per_sec = throughputs[throughputs.size() / 2];  // median across trials
    return s;
}

void printStats(const std::string& name, const Stats& s) {
    std::cout << std::left << std::setw(22) << name << "p50=" << std::right << std::setw(7)
              << std::fixed << std::setprecision(0) << s.p50_ns << "ns  "
              << "p90=" << std::setw(7) << s.p90_ns << "ns  "
              << "p99=" << std::setw(8) << s.p99_ns << "ns  "
              << "p99.9=" << std::setw(9) << s.p999_ns << "ns  "
              << "max=" << std::setw(9) << s.max_ns << "ns  "
              << "throughput=" << std::setw(11) << std::setprecision(0)
              << s.throughput_ops_per_sec << " ops/s\n";
}

std::vector<OrderEvent> loadFeed(const std::string& path) {
    FeedParser parser(path);
    std::vector<OrderEvent> events;
    OrderEvent e{};
    while (parser.next(e)) {
        events.push_back(e);
    }
    return events;
}

// Single-threaded: warms up on the first `warmup` events (discarded), then
// times every remaining apply() call individually so we get a real latency
// distribution, not just a total. Runs kTrials times with a fresh engine
// each time; reports the median throughput across trials and percentiles
// pooled from every trial's latency samples.
template <typename MakeBook>
Stats benchSerial(MakeBook make_book, const std::vector<OrderEvent>& events, size_t warmup) {
    std::vector<double> throughputs;
    std::vector<double> all_latencies_ns;

    for (int trial = 0; trial < kTrials; ++trial) {
        auto book = make_book();
        for (size_t i = 0; i < warmup && i < events.size(); ++i) {
            book.apply(events[i]);
        }

        std::vector<double> latencies_ns;
        latencies_ns.reserve(events.size() - warmup);
        auto start = Clock::now();
        for (size_t i = warmup; i < events.size(); ++i) {
            auto t0 = Clock::now();
            book.apply(events[i]);
            auto t1 = Clock::now();
            latencies_ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
        }
        auto end = Clock::now();
        double wall_s = std::chrono::duration<double>(end - start).count();
        throughputs.push_back(static_cast<double>(latencies_ns.size()) / wall_s);
        all_latencies_ns.insert(all_latencies_ns.end(), latencies_ns.begin(), latencies_ns.end());
    }

    return summarize(all_latencies_ns, throughputs);
}

// Multi-threaded: each thread replays its own pre-partitioned feed (disjoint
// order ids, shared price landscape) against the SAME book instance, each
// recording its own latencies locally to avoid contending on a shared
// vector. Total work scales with thread count (each thread always replays
// its full feed), so compare across engines within a thread-count row, not
// across rows for the same engine.
template <typename MakeBook>
Stats benchConcurrent(MakeBook make_book, const std::vector<std::vector<OrderEvent>>& feeds,
                       int num_threads) {
    std::vector<double> throughputs;
    std::vector<double> all_latencies_ns;

    for (int trial = 0; trial < kTrials; ++trial) {
        auto book = make_book();
        std::vector<std::vector<double>> per_thread_latencies(num_threads);

        auto start = Clock::now();
        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&book, &feeds, &per_thread_latencies, t]() {
                const auto& events = feeds[t];
                per_thread_latencies[t].reserve(events.size());
                for (const auto& event : events) {
                    auto t0 = Clock::now();
                    book.apply(event);
                    auto t1 = Clock::now();
                    per_thread_latencies[t].push_back(
                        std::chrono::duration<double, std::nano>(t1 - t0).count());
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }
        auto end = Clock::now();
        double wall_s = std::chrono::duration<double>(end - start).count();

        uint64_t total_ops = 0;
        for (int t = 0; t < num_threads; ++t) {
            total_ops += per_thread_latencies[t].size();
            all_latencies_ns.insert(all_latencies_ns.end(), per_thread_latencies[t].begin(),
                                     per_thread_latencies[t].end());
        }
        throughputs.push_back(static_cast<double>(total_ops) / wall_s);
    }

    return summarize(all_latencies_ns, throughputs);
}

} // namespace

int main() {
    constexpr int64_t kMinPrice = 0;
    constexpr size_t kPriceRange = 50000;
    constexpr size_t kSerialPoolCapacity = 220000;
    constexpr uint64_t kSerialMaxOrderId = 220000;
    constexpr size_t kConcurrentPoolCapacity = 2000000;
    constexpr uint64_t kConcurrentMaxOrderId = 1700000;

    std::cout << "Loading serial feed...\n";
    auto serial_events = loadFeed("data/bench_serial.csv");
    size_t warmup = serial_events.size() / 10;
    std::cout << "  " << serial_events.size() << " events from data/bench_serial.csv\n\n";

    std::cout << "=== Serial engines: " << kTrials << " trials each, " << warmup
              << " warmup events, " << (serial_events.size() - warmup)
              << " measured events per trial ===\n";
    printStats("TreeOrderBook", benchSerial([] { return TreeOrderBook(); }, serial_events, warmup));
    printStats("FlatOrderBook",
               benchSerial(
                   [&] {
                       return FlatOrderBook(kMinPrice, kPriceRange, kSerialPoolCapacity,
                                             kSerialMaxOrderId);
                   },
                   serial_events, warmup));
    printStats("HeapOrderBook", benchSerial([] { return HeapOrderBook(); }, serial_events, warmup));

    std::cout << "\nLoading partitioned concurrent feeds...\n";
    std::vector<std::vector<OrderEvent>> feeds;
    for (int t = 0; t < 8; ++t) {
        feeds.push_back(loadFeed("data/bench_thread_" + std::to_string(t) + ".csv"));
    }
    uint64_t total_concurrent_events = 0;
    for (const auto& f : feeds) {
        total_concurrent_events += f.size();
    }
    std::cout << "  " << feeds.size() << " feeds, " << total_concurrent_events
              << " events total\n\n";

    for (int threads : {1, 2, 4, 8}) {
        std::cout << "=== Concurrent engines at " << threads << " thread"
                  << (threads > 1 ? "s" : "") << " (" << kTrials << " trials each) ===\n";
        printStats("CoarseLockOrderBook",
                   benchConcurrent(
                       [&] {
                           return CoarseLockOrderBook(kMinPrice, kPriceRange,
                                                       kConcurrentPoolCapacity,
                                                       kConcurrentMaxOrderId);
                       },
                       feeds, threads));
        printStats("FineLockOrderBook",
                   benchConcurrent(
                       [&] {
                           return FineLockOrderBook(kMinPrice, kPriceRange,
                                                     kConcurrentPoolCapacity,
                                                     kConcurrentMaxOrderId);
                       },
                       feeds, threads));
        printStats("LockFreeOrderBook",
                   benchConcurrent(
                       [&] {
                           return LockFreeOrderBook(kMinPrice, kPriceRange,
                                                     kConcurrentPoolCapacity,
                                                     kConcurrentMaxOrderId);
                       },
                       feeds, threads));
        std::cout << "\n";
    }

    return 0;
}