#!/usr/bin/env python3
"""Generate synthetic order feed CSV data for the order book benchmark.

Format: timestamp_ns,type,order_id,side,price,quantity
type: A/C/M (add/cancel/modify), side: B/S (buy/sell)
"""
import argparse
import random
import sys


class OpenOrders:
    """Tracks live order ids with O(1) add/remove/random-pick (swap-remove)."""

    def __init__(self):
        self._ids = []
        self._sides = {}
        self._index = {}

    def add(self, order_id, side):
        self._index[order_id] = len(self._ids)
        self._ids.append(order_id)
        self._sides[order_id] = side

    def remove(self, order_id):
        idx = self._index.pop(order_id)
        last_id = self._ids.pop()
        if idx < len(self._ids):
            self._ids[idx] = last_id
            self._index[last_id] = idx
        return self._sides.pop(order_id)

    def side_of(self, order_id):
        return self._sides[order_id]

    def random_id(self, rng):
        return rng.choice(self._ids)

    def __len__(self):
        return len(self._ids)


def sample_price(rng, mid_price, side, args):
    # cluster orders near the touch, fewer deeper in the book
    offset_ticks = int(rng.expovariate(1.0 / args.spread_ticks)) * args.tick_size
    return mid_price - offset_ticks if side == "B" else mid_price + offset_ticks


def sample_quantity(rng, args):
    # Lognormal: many small orders, a long right tail of large ones.
    qty = int(rng.lognormvariate(args.qty_mu, args.qty_sigma))
    return max(args.qty_min, min(args.qty_max, qty))


def pick_event_type(rng, args, has_open_orders):
    if not has_open_orders:
        return "A"
    roll = rng.random()
    if roll < args.add_prob:
        return "A"
    if roll < args.add_prob + args.cancel_prob:
        return "C"
    return "M"


def generate_rows(args):
    rng = random.Random(args.seed)
    open_orders = OpenOrders()

    mid_price = args.mid_price
    next_order_id = args.start_order_id
    timestamp_ns = 0.0

    for _ in range(args.num_events):
        timestamp_ns += rng.expovariate(1.0 / args.mean_interarrival_ns)
        ts = int(timestamp_ns)

        if rng.random() < args.drift_prob:
            mid_price += rng.choice((-1, 1)) * args.tick_size

        event_type = pick_event_type(rng, args, len(open_orders) > 0)

        if event_type == "A":
            order_id = next_order_id
            next_order_id += 1
            side = "B" if rng.random() < 0.5 else "S"
            price = sample_price(rng, mid_price, side, args)
            qty = sample_quantity(rng, args)
            open_orders.add(order_id, side)
            yield (ts, "A", order_id, side, price, qty)
        elif event_type == "C":
            order_id = open_orders.random_id(rng)
            side = open_orders.remove(order_id)
            yield (ts, "C", order_id, side, 0, 0)
        else:
            order_id = open_orders.random_id(rng)
            side = open_orders.side_of(order_id)
            price = sample_price(rng, mid_price, side, args)
            qty = sample_quantity(rng, args)
            yield (ts, "M", order_id, side, price, qty)


def build_arg_parser():
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-n", "--num-events", type=int, default=10_000,
                         help="number of feed events to generate (default: 10000)")
    parser.add_argument("-o", "--output", default="-",
                         help="output file path, or '-' for stdout (default: stdout)")
    parser.add_argument("--seed", type=int, default=None,
                         help="RNG seed for reproducible output (default: unseeded)")
    parser.add_argument("--start-order-id", type=int, default=1)
    parser.add_argument("--mid-price", type=int, default=10_000,
                         help="starting reference price in ticks (default: 10000)")
    parser.add_argument("--tick-size", type=int, default=1)
    parser.add_argument("--spread-ticks", type=float, default=5.0,
                         help="mean distance from the touch new orders are quoted at")
    parser.add_argument("--drift-prob", type=float, default=0.05,
                         help="probability the mid price randomly walks 1 tick on any event")
    parser.add_argument("--mean-interarrival-ns", type=float, default=1_000.0,
                         help="mean gap between event timestamps, in ns (Poisson arrivals)")
    parser.add_argument("--add-prob", type=float, default=0.7)
    parser.add_argument("--cancel-prob", type=float, default=0.2,
                         help="remainder (1 - add-prob - cancel-prob) is modify probability")
    parser.add_argument("--qty-min", type=int, default=1)
    parser.add_argument("--qty-max", type=int, default=1_000)
    parser.add_argument("--qty-mu", type=float, default=4.0,
                         help="lognormal mu for order quantity")
    parser.add_argument("--qty-sigma", type=float, default=1.0,
                         help="lognormal sigma for order quantity")
    return parser


def main():
    parser = build_arg_parser()
    args = parser.parse_args()

    if args.add_prob + args.cancel_prob > 1.0:
        parser.error("--add-prob + --cancel-prob must be <= 1.0")

    out = sys.stdout if args.output == "-" else open(args.output, "w", newline="\n")
    try:
        for row in generate_rows(args):
            out.write(",".join(str(field) for field in row) + "\n")
    finally:
        if out is not sys.stdout:
            out.close()


if __name__ == "__main__":
    main()