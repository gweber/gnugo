#!/usr/bin/env python3
"""Successive-Halving tuner -- sample-efficient hyperparameter search (AutoML).

Successive Halving / Hyperband (Jamieson & Talwalkar; Li et al.): instead of
spending a fixed budget per candidate (SPSA), sample many random configs,
evaluate them all cheaply, keep the top 1/eta, and re-evaluate the survivors
with eta x more games -- repeating until one remains. Most of the budget goes
to the promising configs; bad ones are killed early. Dependency-free.

This complements partune_mc.py (SPSA): SH is better for a broad/multi-modal
search (it doesn't assume a smooth gradient), e.g. a first sweep before SPSA
polishes the winner, or for the playout-table feature multipliers later.

Each config is scored by win rate vs a fixed baseline engine (parallel match).

Example:
    python3 asha.py \
      --engine "env GNUGO_RAVE=1 GNUGO_MC_AVOID_SELFATARI=1 ../../interface/gnugo --mode gtp --monte-carlo --level 1" \
      --baseline "env GNUGO_RAVE=1 GNUGO_MC_AVOID_SELFATARI=1 ../../interface/gnugo --mode gtp --monte-carlo --level 1" \
      --n 16 --base-games 24 --eta 4 --size 9 --workers 16
"""

import argparse
import random

from parmatch import parallel_match

# name -> (low, high). Same knobs as partune_mc.
PARAMS = {
    "GNUGO_RAVE_EQUIV": (30.0, 400.0),
    "GNUGO_RAVE_C":     (0.10, 0.60),
    "GNUGO_RAVE_FPU":   (0.30, 1.10),
    "GNUGO_MC_SHRINK":  (0.0, 1.0),
}


def sample_config(rng):
    return {k: rng.uniform(lo, hi) for k, (lo, hi) in PARAMS.items()}


def env_cmd(base_cmd, theta):
    return "env " + " ".join(f"{k}={v:.4f}" for k, v in theta.items()) + " " + base_cmd


def score(cfg, engine, baseline, games, size, komi, workers):
    a_wins, b_wins = parallel_match(env_cmd(engine, cfg), baseline,
                                    games, size, komi, workers)
    decided = a_wins + b_wins
    return a_wins / decided if decided else 0.0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", required=True, help="base GTP command to tune (no env)")
    ap.add_argument("--baseline", required=True, help="fixed opponent GTP command")
    ap.add_argument("--n", type=int, default=16, help="initial configs")
    ap.add_argument("--base-games", type=int, default=24, help="games at first rung")
    ap.add_argument("--eta", type=int, default=4, help="halving factor")
    ap.add_argument("--size", type=int, default=9)
    ap.add_argument("--komi", type=float, default=7.5)
    ap.add_argument("--workers", type=int, default=16)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    configs = [sample_config(rng) for _ in range(args.n)]
    games = args.base_games
    rung = 0

    while len(configs) > 1:
        scored = []
        for i, c in enumerate(configs):
            wr = score(c, args.engine, args.baseline, games,
                       args.size, args.komi, args.workers)
            scored.append((wr, c))
            print(f"  rung {rung} [{games}g] config {i+1}/{len(configs)}: "
                  f"{100*wr:4.1f}%", flush=True)
        scored.sort(key=lambda x: x[0], reverse=True)
        keep = max(1, len(configs) // args.eta)
        configs = [c for _, c in scored[:keep]]
        print(f"rung {rung}: kept {keep}/{len(scored)} (best "
              f"{100*scored[0][0]:.1f}%)", flush=True)
        games *= args.eta
        rung += 1

    best = configs[0]
    print("\n# Best config (validate with sprt.py before adopting):")
    for k, v in best.items():
        print(f"export {k}={v:.4f}")


if __name__ == "__main__":
    main()
