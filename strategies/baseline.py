"""Runs simple non-ML policies through the execution environment, as a
point of comparison for the regression-driven policies.
"""
import numpy as np

from env import ExecutionEnv, ACTION_PASSIVE_BUY, NUM_ACTIONS


def run_policy(env, action_fn, num_episodes=200):
    rewards, fills = [], []
    for _ in range(num_episodes):
        env.reset()
        total = 0.0
        while True:
            action = action_fn()
            _, reward, done, _ = env.step(action)
            total += reward
            if done:
                break
        rewards.append(total)
        fills.append((env.target_qty - env.remaining) / env.target_qty)
    return float(np.mean(rewards)), float(np.mean(fills))


def main():
    rng = np.random.default_rng(0)

    print("Random policy (200 episodes)...")
    env = ExecutionEnv(seed=1)
    avg_reward, avg_fill = run_policy(env, lambda: int(rng.integers(0, NUM_ACTIONS)))
    print(f"  avg reward = {avg_reward:.3f}   avg fill rate = {avg_fill:.1%}")

    print("Always-passive-buy policy (200 episodes)...")
    env = ExecutionEnv(seed=1)
    avg_reward, avg_fill = run_policy(env, lambda: ACTION_PASSIVE_BUY)
    print(f"  avg reward = {avg_reward:.3f}   avg fill rate = {avg_fill:.1%}")


if __name__ == "__main__":
    main()
