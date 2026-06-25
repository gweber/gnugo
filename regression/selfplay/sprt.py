#!/usr/bin/env python3
"""SPRT A/B match with common random numbers -- cross-domain tooling.

Two variance/cost reductions borrowed from sister fields:

1. SPRT (Sequential Probability Ratio Test; Wald 1945, standard in chess-engine
   testing / Stockfish fishtest): instead of a fixed game count, test
   H0: Elo<=elo0 vs H1: Elo>=elo1 after each game and STOP as soon as the
   log-likelihood ratio crosses a bound. Clear results resolve in a fraction of
   the games a fixed-N match needs, with bounded type-I/II error.

2. Common Random Numbers (CRN; variance reduction from OR/simulation): play
   games in color-swapped PAIRS that share an RNG seed (sent to both engines
   via set_random_seed), so per-game luck cancels between the paired engines
   and the estimated difference has lower variance.

Games run in parallel waves (reusing twogtp.GtpEngine); the LLR is checked
after each wave, so the stop overshoots by at most one wave -- a fine trade
for keeping the cores busy.

Example:
    python3 sprt.py \
      --a "env GNUGO_RAVE=1 GNUGO_MC_AVOID_SELFATARI=1 ../../interface/gnugo --mode gtp --monte-carlo --level 1" \
      --b "env GNUGO_RAVE=1 ../../interface/gnugo --mode gtp --monte-carlo --level 1" \
      --elo0 0 --elo1 15 --size 9 --workers 16 --crn
"""

import argparse
import concurrent.futures
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from twogtp import GtpEngine, play_game, elo_diff


def elo_to_p(elo):
    return 1.0 / (1.0 + 10.0 ** (-elo / 400.0))


def one_game(idx, cmd_a, cmd_b, size, komi, crn):
    """Play game idx; return +1 (A win), -1 (B win), 0 (draw/undecided).

    With CRN, games are color-swapped pairs sharing a seed: even idx -> A black,
    odd idx -> A white, both with seed idx//2 set on both engines."""
    a_black = (idx % 2 == 0)
    black = GtpEngine(cmd_a if a_black else cmd_b)
    white = GtpEngine(cmd_b if a_black else cmd_a)
    try:
        if crn:
            seed = idx // 2 + 1
            for e in (black, white):
                try:
                    e.send(f"set_random_seed {seed}")
                except Exception:
                    pass
        winner = play_game(black, white, size, komi)
    finally:
        black.close()
        white.close()
    if winner is None:
        return 0
    return 1 if (winner == "B") == a_black else -1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", required=True)
    ap.add_argument("--elo0", type=float, default=0.0, help="H0 Elo bound")
    ap.add_argument("--elo1", type=float, default=15.0, help="H1 Elo bound")
    ap.add_argument("--alpha", type=float, default=0.05)
    ap.add_argument("--beta", type=float, default=0.05)
    ap.add_argument("--size", type=int, default=9)
    ap.add_argument("--komi", type=float, default=7.5)
    ap.add_argument("--workers", type=int, default=16)
    ap.add_argument("--maxgames", type=int, default=4000)
    ap.add_argument("--crn", action="store_true", help="common random numbers")
    args = ap.parse_args()

    p0, p1 = elo_to_p(args.elo0), elo_to_p(args.elo1)
    # SPRT acceptance bounds on the log-likelihood ratio.
    lower = math.log(args.beta / (1.0 - args.alpha))
    upper = math.log((1.0 - args.beta) / args.alpha)
    win_inc = math.log(p1 / p0)
    loss_inc = math.log((1.0 - p1) / (1.0 - p0))

    llr = 0.0
    a_wins = b_wins = draws = 0
    idx = 0
    decision = "inconclusive"

    print(f"SPRT  H0: Elo<={args.elo0}  H1: Elo>={args.elo1}  "
          f"(alpha={args.alpha}, beta={args.beta}; bounds [{lower:.2f},{upper:.2f}])"
          f"{'  +CRN' if args.crn else ''}", flush=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as ex:
        while idx < args.maxgames:
            wave = [ex.submit(one_game, idx + j, args.a, args.b,
                              args.size, args.komi, args.crn)
                    for j in range(args.workers)]
            idx += args.workers
            for f in wave:
                r = f.result()
                if r == 1:
                    a_wins += 1
                    llr += win_inc
                elif r == -1:
                    b_wins += 1
                    llr += loss_inc
                else:
                    draws += 1
            decided = a_wins + b_wins
            elo, band = elo_diff(a_wins, decided) if decided else (0.0, 0.0)
            print(f"  games={a_wins+b_wins+draws:4d}  A={a_wins} B={b_wins} "
                  f"D={draws}  LLR={llr:+.2f}  Elo~{elo:+.0f}", flush=True)
            if llr >= upper:
                decision = "H1 accepted (A is stronger by >= elo0..elo1)"
                break
            if llr <= lower:
                decision = "H0 accepted (A is NOT stronger than elo0)"
                break

    decided = a_wins + b_wins
    elo, band = elo_diff(a_wins, decided) if decided else (0.0, 0.0)
    print("-" * 56)
    print(f"decision: {decision}")
    print(f"games: {a_wins+b_wins+draws}  A_wins={a_wins} B_wins={b_wins} draws={draws}")
    print(f"final LLR={llr:+.2f}  Elo~{elo:+.0f} +/- {band:.0f}")


if __name__ == "__main__":
    main()
