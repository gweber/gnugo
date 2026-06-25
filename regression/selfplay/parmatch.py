#!/usr/bin/env python3
"""Parallel self-play match runner.

Self-play games are independent, so they parallelize trivially across cores.
This plays a match of N games between two engine commands across W worker
threads (each game drives its own pair of gnugo subprocesses; the blocking
pipe reads release the GIL, so threads scale with cores). Colors alternate by
game index for fairness. Importable -- partune.py reuses parallel_match().

It reuses GtpEngine/play_game/elo_diff from twogtp.py read-only (no edits to
the shared harness).

Example:
    python3 parmatch.py \
      --a "env GNUGO_TERRITORIAL_WEIGHT=1.2 ../../interface/gnugo --mode gtp --level 1" \
      --b "../../interface/gnugo --mode gtp --level 1" \
      --games 400 --size 13 --workers 16
"""

import argparse
import concurrent.futures
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from twogtp import GtpEngine, play_game, elo_diff


def _one_game(idx, cmd_a, cmd_b, size, komi):
    """Play game `idx`; return +1 if A wins, -1 if B wins, 0 if undecided."""
    a_black = (idx % 2 == 0)
    black = GtpEngine(cmd_a if a_black else cmd_b)
    white = GtpEngine(cmd_b if a_black else cmd_a)
    try:
        winner = play_game(black, white, size, komi)
    finally:
        black.close()
        white.close()
    if winner is None:
        return 0
    a_won = (winner == "B") == a_black
    return 1 if a_won else -1


def parallel_match(cmd_a, cmd_b, games, size, komi, workers):
    """Return (a_wins, b_wins) over `games` games run `workers`-at-a-time."""
    a_wins = b_wins = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
        futures = [ex.submit(_one_game, i, cmd_a, cmd_b, size, komi)
                   for i in range(games)]
        for f in concurrent.futures.as_completed(futures):
            r = f.result()
            if r == 1:
                a_wins += 1
            elif r == -1:
                b_wins += 1
    return a_wins, b_wins


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--a", required=True, help="engine A GTP command")
    ap.add_argument("--b", required=True, help="engine B GTP command")
    ap.add_argument("--games", type=int, default=200)
    ap.add_argument("--size", type=int, default=13)
    ap.add_argument("--komi", type=float, default=7.5)
    ap.add_argument("--workers", type=int, default=16)
    args = ap.parse_args()

    import time
    t0 = time.time()
    a_wins, b_wins = parallel_match(args.a, args.b, args.games,
                                    args.size, args.komi, args.workers)
    dt = time.time() - t0
    decided = a_wins + b_wins
    elo, band = elo_diff(a_wins, decided)
    print(f"A: {args.a}")
    print(f"B: {args.b}")
    print(f"games={args.games} workers={args.workers} size={args.size}  "
          f"A_wins={a_wins} B_wins={b_wins} draws={args.games - decided}")
    if decided:
        print(f"A win rate: {100.0 * a_wins / decided:.1f}%")
        print(f"A Elo vs B: {elo:+.0f} +/- {band:.0f}")
    print(f"elapsed: {dt:.1f}s ({dt / max(args.games,1):.2f}s/game wall, "
          f"{args.workers}x parallel)")


if __name__ == "__main__":
    main()
