#!/usr/bin/env python3
"""Parallel SPSA tuner for the Monte Carlo / RAVE stack (full stack enabled).

Tunes the *untuned* MC knobs jointly, with the confirmed wins (RAVE +
self-atari) switched on, so the result is the optimum for the stack we
actually ship -- this is the "tune everything together" pass. Same parallel
SPSA as partune.py; MC games are slow, so it leans on parmatch's parallelism.

The base engine command already fixes the enabled features; this tuner only
perturbs the continuous knobs below.

Example (9x9, the only board MC currently runs on):
    python3 partune_mc.py \
      --engine "env GNUGO_RAVE=1 GNUGO_MC_AVOID_SELFATARI=1 ../../interface/gnugo --mode gtp --monte-carlo --level 1" \
      --iters 25 --games 40 --size 9 --workers 16
"""

import argparse
import random

from parmatch import parallel_match

# name -> (default, low, high). RAVE constants start from the already-tuned
# values; the two *_P knobs are the Moggy-style fire-probabilities for the
# tactical override rules (atari-response, LGRF) -- start at 0 (off) so SPSA
# discovers whether a *dose* of them helps, given the constants can co-adapt.
PARAMS = {
    "GNUGO_RAVE_EQUIV":  (530.0, 100.0, 4000.0),
    "GNUGO_RAVE_C":      (0.41, 0.05, 1.5),
    "GNUGO_RAVE_FPU":    (0.57, 0.1, 1.2),
    "GNUGO_MC_ATARI_P":  (0.0, 0.0, 1.0),
    "GNUGO_MC_LGRF_P":   (0.0, 0.0, 1.0),
}


def clamp(x, lo, hi):
    return max(lo, min(hi, x))


def env_cmd(base_cmd, theta):
    env = " ".join(f"{k}={v:.4f}" for k, v in theta.items())
    return f"env {env} {base_cmd}"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", required=True, help="base GTP command (enabled stack)")
    ap.add_argument("--iters", type=int, default=25)
    ap.add_argument("--games", type=int, default=40)
    ap.add_argument("--size", type=int, default=9)
    ap.add_argument("--komi", type=float, default=7.5)
    ap.add_argument("--workers", type=int, default=16)
    ap.add_argument("--a", type=float, default=0.10)
    ap.add_argument("--c", type=float, default=0.12)
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
            print(f"iter {it}: no decided games", flush=True)
            continue
        grad_sign = (a_wins / decided - 0.5) * 2.0
        for k, (_, lo, hi) in PARAMS.items():
            theta[k] = clamp(theta[k] + ak * span[k] * grad_sign * delta[k], lo, hi)
        print(f"iter {it:3d}: (+) {100*a_wins/decided:5.1f}%  -> "
              + "  ".join(f"{k.replace('GNUGO_','').lower()}={theta[k]:.3f}"
                          for k in PARAMS), flush=True)

    print("\n# Tuned MC stack (validate with parmatch before adopting):")
    for k in PARAMS:
        print(f"export {k}={theta[k]:.4f}")


if __name__ == "__main__":
    main()
