#include "orderbook/feed_parser.h"

#include <charconv>
#include <array>

namespace orderbook {

namespace {

// Splits into up to 6 comma-separated fields, returns count found
size_t splitFields(std::string_view line, std::array<std::string_view, 6>& fields) {
    size_t count = 0;
    size_t start = 0;
    while (count < fields.size()) {
        size_t comma = line.find(',', start);
        if (comma == std::string_view::npos) {
            fields[count++] = line.substr(start);
            break;
        }
        fields[count++] = line.substr(start, comma - start);
        start = comma + 1;
    }
    return count;
}

template <typename T>
bool parseInt(std::string_view field, T& out) {
    auto result = std::from_chars(field.data(), field.data() + field.size(), out);
    return result.ec == std::errc{} && result.ptr == field.data() + field.size();
}

} // namespace

std::optional<OrderEvent> parseLine(std::string_view line) {
    // handle CRLF line endings
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    if (line.empty()) {
        return std::nullopt;
    }

    std::array<std::string_view, 6> fields{};
    if (splitFields(line, fields) != 6) {
        return std::nullopt;
    }

    OrderEvent event{};

    if (!parseInt(fields[0], event.timestamp_ns)) {
        return std::nullopt;
    }

    if (fields[1] == "A") {
        event.type = EventType::Add;
    } else if (fields[1] == "C") {
        event.type = EventType::Cancel;
    } else if (fields[1] == "M") {
        event.type = EventType::Modify;
    } else {
        return std::nullopt;
    }

    if (!parseInt(fields[2], event.order_id)) {
        return std::nullopt;
    }

    if (fields[3] == "B") {
        event.side = Side::Buy;
    } else if (fields[3] == "S") {
        event.side = Side::Sell;
    } else {
        return std::nullopt;
    }

    if (!parseInt(fields[4], event.price)) {
        return std::nullopt;
    }
    if (!parseInt(fields[5], event.quantity)) {
        return std::nullopt;
    }

    return event;
}

FeedParser::FeedParser(const std::string& path) : file_(path) {}

bool FeedParser::next(OrderEvent& out) {
    std::string line;
    while (std::getline(file_, line)) {
        if (auto event = parseLine(line)) {
            out = *event;
            return true;
        }
    }
    return false;
}

} // namespace orderbook