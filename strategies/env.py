"""Execution environment: buy TARGET_QTY shares within EPISODE_LEN steps,
trading alongside simulated background market flow, as close to the
arrival mid-price as possible.
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
AGENT_ID_BASE = 100_000  # reset every episode; only needs to beat the background feed's own ids

ACTION_NOOP = 0
ACTION_PASSIVE_BUY = 1
ACTION_AGGRESSIVE_BUY = 2
ACTION_CANCEL = 3
NUM_ACTIONS = 4

SLICE_QTY = 20
TIME_PENALTY = 0.001
UNFILLED_PENALTY_PER_UNIT = 8.0  # lower values let the agent skip finishing the order


def _background_args(seed, num_events):
    return argparse.Namespace(
        num_events=num_events, start_order_id=1, mid_price=MID_PRICE, tick_size=1,
        spread_ticks=5.0, drift_prob=0.05, mean_interarrival_ns=1000.0, add_prob=0.7,
        cancel_prob=0.2, qty_min=1, qty_max=50, qty_mu=2.5, qty_sigma=1.0, seed=seed)


class ExecutionEnv:
    def __init__(self, target_qty=200, episode_len=150, background_warmup=300, seed=None):
        self.target_qty = target_qty
        self.episode_len = episode_len
        self.background_warmup = background_warmup
        self._rng = np.random.default_rng(seed)
        self._agent_next_id = AGENT_ID_BASE
        self.book = None
        self._background = None
        self.remaining = 0
        self.step_count = 0
        self.arrival_mid = MID_PRICE
        self.outstanding_order_id = None

    def _apply_background_event(self, row):
        ts, kind, order_id, side_ch, price, qty = row
        side = ob.Side.Buy if side_ch == "B" else ob.Side.Sell
        if kind == "A":
            self.book.add_order(order_id, side, price, qty, ts)
        elif kind == "C":
            self.book.cancel_order(order_id)
        else:  # modify: cancel-replace, same convention as the C++ engines
            self.book.cancel_order(order_id)
            self.book.add_order(order_id, side, price, qty, ts)

    def _mid(self):
        bid = self.book.best_bid()
        ask = self.book.best_ask()
        if bid is None and ask is None:
            return float(MID_PRICE)
        if bid is None:
            return float(ask)
        if ask is None:
            return float(bid)
        return (bid + ask) / 2.0

    def _observation(self):
        bid = self.book.best_bid()
        ask = self.book.best_ask()
        mid = self._mid()
        bid_qty = self.book.quantity_at(ob.Side.Buy, bid) if bid is not None else 0
        ask_qty = self.book.quantity_at(ob.Side.Sell, ask) if ask is not None else 0
        spread = (ask - bid) if (bid is not None and ask is not None) else 10.0
        return np.array([
            (mid - self.arrival_mid) / 10.0,
            spread / 10.0,
            np.log1p(bid_qty) / 5.0,
            np.log1p(ask_qty) / 5.0,
            self.remaining / self.target_qty,
            1.0 - self.step_count / self.episode_len,
            1.0 if self.outstanding_order_id is not None else 0.0,
        ], dtype=np.float64)

    def reset(self):
        seed = int(self._rng.integers(0, 2**31 - 1))
        total_events = self.background_warmup + self.episode_len + 5
        self._background = generate_rows(_background_args(seed, total_events))
        self.book = ob.FlatOrderBook(MIN_PRICE, PRICE_RANGE, 5_000, 150_000)
        self._agent_next_id = AGENT_ID_BASE  # ids only need to be unique within one episode

        for _ in range(self.background_warmup):
            self._apply_background_event(next(self._background))

        self.arrival_mid = self._mid()
        self.remaining = self.target_qty
        self.step_count = 0
        self.outstanding_order_id = None
        return self._observation()

    def _new_agent_id(self):
        self._agent_next_id += 1
        return self._agent_next_id

    def step(self, action):
        reward = -TIME_PENALTY
        filled_this_step = 0

        if action == ACTION_CANCEL:
            if self.outstanding_order_id is not None:
                self.book.cancel_order(self.outstanding_order_id)
                self.outstanding_order_id = None

        elif action in (ACTION_PASSIVE_BUY, ACTION_AGGRESSIVE_BUY):
            qty = min(SLICE_QTY, self.remaining)
            if action == ACTION_PASSIVE_BUY:
                bid = self.book.best_bid()
                price = bid if bid is not None else MID_PRICE - 5
            else:
                ask = self.book.best_ask()
                price = ask if ask is not None else MID_PRICE + 5

            order_id = self._new_agent_id()
            trades = self.book.add_order(order_id, ob.Side.Buy, price, qty, 0)
            for t in trades:
                filled_this_step += t.quantity
                reward -= (t.price - self.arrival_mid) * (t.quantity / self.target_qty)
            remaining_after = qty - filled_this_step
            self.outstanding_order_id = order_id if remaining_after > 0 else None

        self.remaining -= filled_this_step
        self.step_count += 1
        done = self.remaining <= 0 or self.step_count >= self.episode_len

        if self.step_count < self.episode_len and not done:
            self._apply_background_event(next(self._background))

        if done and self.remaining > 0:
            reward -= UNFILLED_PENALTY_PER_UNIT * (self.remaining / self.target_qty)

        return self._observation(), reward, done, {"filled": filled_this_step}
