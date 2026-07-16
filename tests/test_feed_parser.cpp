#include "orderbook/feed_parser.h"
#include "test_framework.h"

using namespace orderbook;

TEST(parses_valid_add_line) {
    auto event = parseLine("1000,A,1,B,10050,100");
    CHECK(event.has_value());
    CHECK_EQ(event->timestamp_ns, 1000u);
    CHECK(event->type == EventType::Add);
    CHECK_EQ(event->order_id, 1u);
    CHECK(event->side == Side::Buy);
    CHECK_EQ(event->price, 10050);
    CHECK_EQ(event->quantity, 100u);
}

TEST(parses_sell_and_cancel_and_modify) {
    auto sell = parseLine("2000,A,2,S,10060,50");
    CHECK(sell.has_value());
    CHECK(sell->side == Side::Sell);

    auto cancel = parseLine("3000,C,1,B,0,0");
    CHECK(cancel.has_value());
    CHECK(cancel->type == EventType::Cancel);

    auto modify = parseLine("4000,M,2,S,10055,25");
    CHECK(modify.has_value());
    CHECK(modify->type == EventType::Modify);
    CHECK_EQ(modify->price, 10055);
}

TEST(trims_trailing_carriage_return) {
    auto event = parseLine("1000,A,1,B,10050,100\r");
    CHECK(event.has_value());
    CHECK_EQ(event->quantity, 100u);
}

TEST(rejects_malformed_lines) {
    CHECK(!parseLine("").has_value());
    CHECK(!parseLine("not,enough,fields").has_value());
    CHECK(!parseLine("1000,X,1,B,10050,100").has_value());  // bad type
    CHECK(!parseLine("1000,A,1,Z,10050,100").has_value());  // bad side
    CHECK(!parseLine("abc,A,1,B,10050,100").has_value());   // bad integer
}