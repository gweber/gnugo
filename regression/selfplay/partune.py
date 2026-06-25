#!/usr/bin/env python3
"""Parallel SPSA tuner for GNU Go's runtime-tunable weights.

Same SPSA method as tune.py, but each iteration's perturbation match is played
in parallel across cores via parmatch.parallel_match -- so a 40-game iteration
finishes in a few seconds instead of ~a minute, and a real tuning run takes
minutes rather than an hour. Self-contained (new file) so the shared tune.py /
twogtp.py are never modified.

Example (13x13, the board where the territorial/strategic weights actually
bite, 16 workers):
    python3 partune.py --engine "../../interface/gnugo --mode gtp --level 1" \
        --iters 40 --games 40 --size 13 --workers 16
"""

import argparse
import random

from parmatch import parallel_match

# name -> (default, low, high). Mirrors engine/params.h defaults.
PARAMS = {
    "GNUGO_TERRITORIAL_WEIGHT":    (1.0, 0.5, 2.0),
    "GNUGO_STRATEGICAL_WEIGHT":    (1.0, 0.5, 2.0),
    "GNUGO_ATTACK_DRAGON_WEIGHT":  (1.0, 0.5, 2.0),
    "GNUGO_FOLLOWUP_WEIGHT":       (1.0, 0.5, 2.0),
    "GNUGO_INVASION_MALUS_WEIGHT": (1.0, 0.2, 3.0),
    "GNUGO_SHAPE_FACTOR_BASE":     (1.05, 1.0, 1.15),
    "GNUGO_LUNCH_MULTIPLIER":      (1.8, 1.0, 3.0),
}


def clamp(x, lo, hi):
    return max(lo, min(hi, x))


def env_cmd(base_cmd, theta):
    env = " ".join(f"{k}={v:.4f}" for k, v in theta.items())
    return f"env {env} {base_cmd}"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", required=True, help="base GTP command (no env)")
    ap.add_argument("--iters", type=int, default=40)
    ap.add_argument("--games", type=int, default=40, help="games per iteration")
    ap.add_argument("--size", type=int, default=13)
    ap.add_argument("--komi", type=float, default=7.5)
    ap.add_argument("--workers", type=int, default=16)
    ap.add_argument("--a", type=float, default=0.08, help="SPSA step size")
    ap.add_argument("--c", type=float, default=0.10, help="SPSA perturbation size")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    theta = {k: p[0] for k, p in PARAMS.items()}
    span = {k: (hi - lo) for k, (_, lo, hi) in PARAMS.items()}

    for it in range(1, args.iters + 1):
        ck = args.c / (it ** 0.101)
        ak = args.a / (it ** 0.602)
        delta = {k: (1 if rng.random() < 0.5 else -1) for k in PARAMS}

        plus = {k: clamp(theta[k] + ck * span[k] * delta[k], lo, hi)
                for k, (_, lo, hi) in PARAMS.items()}
        minus = {k: clamp(theta[k] - ck * span[k] * delta[k], lo, hi)
                 for k, (_, lo, hi) in PARAMS.items()}

        a_wins, b_wins = parallel_match(env_cmd(args.engine, plus),
                                        env_cmd(args.engine, minus),
                                        args.games, args.size, args.komi,
                                        args.workers)
        decided = a_wins + b_wins
        if decided == 0:
            print(f"iter {it}: no decided games, skipping", flush=True)
            continue
        winrate = a_wins / decided
        grad_sign = (winrate - 0.5) * 2.0
        for k, (_, lo, hi) in PARAMS.items():
            theta[k] = clamp(theta[k] + ak * span[k] * grad_sign * delta[k], lo, hi)

        print(f"iter {it:3d}: (+) winrate {100*winrate:5.1f}%  ->  "
              + "  ".join(f"{k.replace('GNUGO_','').lower()}={theta[k]:.3f}"
                          for k in PARAMS), flush=True)

    print("\n# Tuned parameters (validate with parmatch before committing):")
    for k in PARAMS:
        print(f"export {k}={theta[k]:.4f}")


if __name__ == "__main__":
    main()
