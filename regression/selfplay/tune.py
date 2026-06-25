#!/usr/bin/env python3
"""SPSA tuner for GNU Go's runtime-tunable strength parameters.

Optimizes the GNUGO_* parameters (see ../../engine/params.h) by self-play,
using Simultaneous Perturbation Stochastic Approximation -- the standard
method for tuning noisy game-engine parameters (cf. Coulom's CLOP; SPSA is
used by Stockfish & friends for the same job).

Each iteration perturbs ALL parameters at once by +/-delta, plays a match
between the (+) and (-) parameter sets, and nudges every parameter toward the
side that won. This needs only one match per iteration regardless of
dimensionality -- the property that makes SPSA practical here.

Example:
    python3 tune.py --engine "../../interface/gnugo --mode gtp --level 1" \
                    --iters 40 --games 24 --size 9

The result is a set of GNUGO_* values you can A/B test against the defaults
with selfplay.sh before committing them as new defaults in globals.c.
"""

import argparse
import random

from twogtp import GtpEngine, play_game, elo_diff

# name -> (default, low, high). Defaults mirror globals.c / params.h.
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
    """Wrap base_cmd so the engine runs with the given parameter values."""
    env = " ".join(f"{k}={v:.4f}" for k, v in theta.items())
    return f"env {env} {base_cmd}"


def match(base_cmd, theta_a, theta_b, games, size, komi):
    """Return wins of A out of decided games (A=theta_a, B=theta_b)."""
    a_cmd, b_cmd = env_cmd(base_cmd, theta_a), env_cmd(base_cmd, theta_b)
    a_wins = b_wins = 0
    for g in range(games):
        a_black = (g % 2 == 0)
        black = GtpEngine(a_cmd if a_black else b_cmd)
        white = GtpEngine(b_cmd if a_black else a_cmd)
        try:
            winner = play_game(black, white, size, komi)
        finally:
            black.close()
            white.close()
        if winner is None:
            continue
        if (winner == "B") == a_black:
            a_wins += 1
        else:
            b_wins += 1
    return a_wins, a_wins + b_wins


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", required=True, help="base GTP command (no env)")
    ap.add_argument("--iters", type=int, default=30)
    ap.add_argument("--games", type=int, default=24, help="games per iteration")
    ap.add_argument("--size", type=int, default=9)
    ap.add_argument("--komi", type=float, default=7.5)
    ap.add_argument("--a", type=float, default=0.08, help="SPSA step size")
    ap.add_argument("--c", type=float, default=0.10, help="SPSA perturbation size")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    theta = {k: p[0] for k, p in PARAMS.items()}

    for it in range(1, args.iters + 1):
        # Random simultaneous perturbation (Bernoulli +/-1 per parameter).
        ck = args.c / (it ** 0.101)
        ak = args.a / (it ** 0.602)
        delta = {k: (1 if rng.random() < 0.5 else -1) for k in PARAMS}
        span = {k: (hi - lo) for k, (_, lo, hi) in PARAMS.items()}

        plus = {k: clamp(theta[k] + ck * span[k] * delta[k], lo, hi)
                for k, (_, lo, hi) in PARAMS.items()}
        minus = {k: clamp(theta[k] - ck * span[k] * delta[k], lo, hi)
                 for k, (_, lo, hi) in PARAMS.items()}

        a_wins, decided = match(args.engine, plus, minus,
                                args.games, args.size, args.komi)
        if decided == 0:
            print(f"iter {it}: no decided games, skipping")
            continue
        # Gradient estimate: if (+) side won, push theta toward (+).
        winrate = a_wins / decided
        grad_sign = (winrate - 0.5) * 2.0  # in [-1, 1]
        for k, (_, lo, hi) in PARAMS.items():
            step = ak * span[k] * grad_sign * delta[k]
            theta[k] = clamp(theta[k] + step, lo, hi)

        print(f"iter {it:3d}: (+) winrate {100*winrate:5.1f}%  ->  "
              + "  ".join(f"{k.replace('GNUGO_','').lower()}={theta[k]:.3f}"
                          for k in PARAMS), flush=True)

    print("\n# Tuned parameters (export and A/B test before committing):")
    for k in PARAMS:
        print(f"export {k}={theta[k]:.4f}")


if __name__ == "__main__":
    main()
