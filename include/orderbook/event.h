#pragma once

#include <cstdint>

namespace orderbook {

enum class Side : uint8_t { Buy, Sell };

enum class EventType : uint8_t { Add, Cancel, Modify };

// price is in integer ticks, not floating point
struct OrderEvent {
    uint64_t timestamp_ns;
    uint64_t order_id;
    EventType type;
    Side side;
    int64_t price;
    uint32_t quantity;
};

} // namespace orderbook