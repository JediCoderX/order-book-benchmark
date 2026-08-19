"""Builds a labeled dataset: replay background market flow, and at each
step record LOB features plus a label, the mid-price move over the
next HORIZON steps.
"""
import argparse
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "build"))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))

import orderbook_env as ob  # noqa: E402
from generate_feed import generate_rows  # noqa: E402

MIN_PRICE = 0
PRICE_RANGE = 4000
MID_PRICE = 2000
LOOKBACK = 10
HORIZON = 20


def _background_args(seed, num_events):
    return argparse.Namespace(
        num_events=num_events, start_order_id=1, mid_price=MID_PRICE, tick_size=1,
        spread_ticks=5.0, drift_prob=0.05, mean_interarrival_ns=1000.0, add_prob=0.7,
        cancel_prob=0.2, qty_min=1, qty_max=50, qty_mu=2.5, qty_sigma=1.0, seed=seed)


def _mid(book, fallback):
    bid, ask = book.best_bid(), book.best_ask()
    if bid is None and ask is None:
        return fallback
    if bid is None:
        return float(ask)
    if ask is None:
        return float(bid)
    return (bid + ask) / 2.0


def _features(book, mid_now, mid_history):
    bid, ask = book.best_bid(), book.best_ask()
    spread = (ask - bid) if (bid is not None and ask is not None) else 10.0
    bid_qty = book.quantity_at(ob.Side.Buy, bid) if bid is not None else 0
    ask_qty = book.quantity_at(ob.Side.Sell, ask) if ask is not None else 0
    imbalance = (bid_qty - ask_qty) / (bid_qty + ask_qty + 1.0)
    recent_change = (mid_now - mid_history[-LOOKBACK]) if len(mid_history) >= LOOKBACK else 0.0
    return [spread, np.log1p(bid_qty), np.log1p(ask_qty), imbalance, recent_change]


def collect_episode(seed, num_events):
    book = ob.FlatOrderBook(MIN_PRICE, PRICE_RANGE, 5_000, 150_000)
    mid_history = []
    feature_rows = []

    for row in generate_rows(_background_args(seed, num_events)):
        ts, kind, order_id, side_ch, price, qty = row
        side = ob.Side.Buy if side_ch == "B" else ob.Side.Sell
        if kind == "A":
            book.add_order(order_id, side, price, qty, ts)
        elif kind == "C":
            book.cancel_order(order_id)
        else:
            book.cancel_order(order_id)
            book.add_order(order_id, side, price, qty, ts)

        mid = _mid(book, mid_history[-1] if mid_history else float(MID_PRICE))
        feature_rows.append(_features(book, mid, mid_history))
        mid_history.append(mid)

    mids = np.array(mid_history)
    features = np.array(feature_rows)
    # label[t] = mid[t + HORIZON] - mid[t], only keep rows with a real future
    valid = len(mids) - HORIZON
    X = features[:valid]
    y = mids[HORIZON:HORIZON + valid] - mids[:valid]
    return X, y


def build_dataset(num_episodes, events_per_episode, seed0=0):
    xs, ys = [], []
    for i in range(num_episodes):
        X, y = collect_episode(seed0 + i, events_per_episode)
        xs.append(X)
        ys.append(y)
    return np.concatenate(xs), np.concatenate(ys)


if __name__ == "__main__":
    X, y = build_dataset(num_episodes=20, events_per_episode=2000)
    print(f"dataset: {X.shape[0]} samples, {X.shape[1]} features")
    print(f"label (mid-price change over {HORIZON} steps): mean={y.mean():.4f} std={y.std():.4f}")
