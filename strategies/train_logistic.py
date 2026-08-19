"""Trains logistic regression to predict price direction, then runs a
threshold + urgency-fallback policy built from it through the execution
environment.
"""
import numpy as np

from collect_data import build_dataset, _features, HORIZON
from logistic_regression import LogisticRegression
from env import ExecutionEnv, ACTION_AGGRESSIVE_BUY, ACTION_PASSIVE_BUY

TRAIN_EPISODES = 30
TEST_EPISODES = 10
EVENTS_PER_EPISODE = 2000


def train_and_evaluate():
    print("Collecting training data...")
    X_train, y_train = build_dataset(TRAIN_EPISODES, EVENTS_PER_EPISODE, seed0=0)
    print("Collecting held-out test data (disjoint seeds)...")
    X_test, y_test = build_dataset(TEST_EPISODES, EVENTS_PER_EPISODE, seed0=10_000)
    print(f"train: {X_train.shape[0]} samples   test: {X_test.shape[0]} samples\n")

    mean_val, std_val = X_train.mean(axis=0), X_train.std(axis=0) + 1e-8
    X_train_n = (X_train - mean_val) / std_val
    X_test_n = (X_test - mean_val) / std_val

    y_train_dir = (y_train > 0).astype(float)
    y_test_dir = (y_test > 0).astype(float)
    model = LogisticRegression().fit(X_train_n, y_train_dir)
    pred_dir = model.predict(X_test_n)
    accuracy = np.mean(pred_dir == y_test_dir)
    majority_baseline = max(y_test_dir.mean(), 1 - y_test_dir.mean())

    print(f"Logistic regression: predict price direction over {HORIZON} steps")
    print(f"  accuracy = {accuracy:.4f}   (always-predict-majority-class = "
          f"{majority_baseline:.4f})")

    return model, mean_val, std_val


def policy_action(env, model, mean_val, std_val, mid_history, threshold=0.55,
                   urgency_fraction=0.15):
    mid = env.arrival_mid if not mid_history else mid_history[-1]
    bid, ask = env.book.best_bid(), env.book.best_ask()
    if bid is not None and ask is not None:
        mid = (bid + ask) / 2.0
    elif bid is not None:
        mid = float(bid)
    elif ask is not None:
        mid = float(ask)

    features = np.array(_features(env.book, mid, mid_history))
    mid_history.append(mid)
    features_n = (features - mean_val) / std_val
    prob_up = model.predict_proba(features_n[None, :])[0]

    # aggressive if price looks likely to rise, or time is running out
    time_left_fraction = 1.0 - env.step_count / env.episode_len
    running_out_of_time = time_left_fraction < urgency_fraction
    return ACTION_AGGRESSIVE_BUY if (prob_up > threshold or running_out_of_time) else ACTION_PASSIVE_BUY


def run_policy(env, model, mean_val, std_val, num_episodes=200):
    rewards, fills = [], []
    for _ in range(num_episodes):
        env.reset()
        mid_history = []
        total = 0.0
        while True:
            action = policy_action(env, model, mean_val, std_val, mid_history)
            _, reward, done, _ = env.step(action)
            total += reward
            if done:
                break
        rewards.append(total)
        fills.append((env.target_qty - env.remaining) / env.target_qty)
    return float(np.mean(rewards)), float(np.mean(fills))


def main():
    model, mean_val, std_val = train_and_evaluate()

    print("\nRunning the logistic-regression-driven policy (200 episodes)...")
    env = ExecutionEnv(seed=1)
    avg_reward, avg_fill = run_policy(env, model, mean_val, std_val)
    print(f"  avg reward = {avg_reward:.3f}   avg fill rate = {avg_fill:.1%}")


if __name__ == "__main__":
    main()
