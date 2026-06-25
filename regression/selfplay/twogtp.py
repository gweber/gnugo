#!/usr/bin/env python3
"""Minimal two-engine GTP referee for self-play strength measurement.

Pits two GTP engines against each other for a number of games (alternating
colors), scores each game with the engines' own `final_score`, and reports
the win/loss split plus an Elo estimate with a confidence interval.

This is deliberately dependency-free (stdlib only) so it can run anywhere the
engine builds -- no gogui / gnugo-twogtp required.

Example:
    # new build vs the reference build, 40 games on 9x9 at level 1
    python3 twogtp.py \
        --black "../../interface/gnugo --mode gtp --level 1" \
        --white "/path/to/reference/gnugo --mode gtp --level 1" \
        --games 40 --size 9 --komi 7.5
"""

import argparse
import math
import re
import shlex
import subprocess
import sys
import time


class GtpEngine:
    """A GTP engine driven over stdin/stdout."""

    def __init__(self, command):
        self.command = command
        self.proc = subprocess.Popen(
            shlex.split(command),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )

    def send(self, cmd):
        """Send one GTP command, return the response body (without the '= ')."""
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()
        lines = []
        while True:
            line = self.proc.stdout.readline()
            if line == "":  # engine died
                raise RuntimeError(f"engine '{self.command}' closed the pipe")
            line = line.rstrip("\n")
            if line == "" and lines:
                break
            if line == "" and not lines:
                continue
            lines.append(line)
        body = "\n".join(lines)
        status, _, rest = body.partition(" ")
        if status.startswith("?"):
            raise RuntimeError(f"GTP error from '{self.command}': {rest}")
        return rest.strip()

    def close(self):
        try:
            self.send("quit")
        except Exception:
            pass
        try:
            self.proc.terminate()
        except Exception:
            pass


def play_game(black, white, size, komi, seed=None):
    """Play one game. Return 'B' or 'W' (winner) or None if undecided."""
    for eng in (black, white):
        eng.send(f"boardsize {size}")
        eng.send(f"komi {komi}")
        eng.send("clear_board")
        # Pin the engine's RNG for this game so the match is reproducible and
        # the games are independent of wall-clock startup time. Both engines
        # share the seed, which pairs the "luck" and reduces variance in the
        # A-vs-B comparison. Harmless for deterministic (non-Monte-Carlo)
        # engines, which ignore the RNG.
        if seed is not None:
            eng.send(f"set_random_seed {seed}")

    engines = {"black": black, "white": white}
    passes = 0
    to_move = "black"
    max_moves = size * size * 3  # generous safety cap

    for _ in range(max_moves):
        mover = engines[to_move]
        other = engines["white" if to_move == "black" else "black"]
        move = mover.send(f"genmove {to_move}").lower()

        if move == "resign":
            return "W" if to_move == "black" else "B"
        if move == "pass":
            passes += 1
        else:
            passes = 0
        other.send(f"play {to_move} {move}")
        if passes >= 2:
            break
        to_move = "white" if to_move == "black" else "black"

    # Score from black's perspective; cross-check with white.
    score = black.send("final_score")
    m = re.match(r"([BW])\+", score)
    return m.group(1) if m else None


def elo_diff(wins, games):
    """Elo difference of the player with `wins` wins out of `games`."""
    if games == 0:
        return 0.0, 0.0
    p = wins / games
    p = min(max(p, 1e-9), 1 - 1e-9)
    elo = -400.0 * math.log10(1.0 / p - 1.0)
    # 95% CI on the win rate -> Elo band
    se = math.sqrt(p * (1 - p) / games)
    lo = min(max(p - 1.96 * se, 1e-9), 1 - 1e-9)
    hi = min(max(p + 1.96 * se, 1e-9), 1 - 1e-9)
    band = (
        -400.0 * math.log10(1.0 / hi - 1.0) - (-400.0 * math.log10(1.0 / lo - 1.0))
    ) / 2.0
    return elo, abs(band)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--black", required=True, help="GTP command for engine A")
    ap.add_argument("--white", required=True, help="GTP command for engine B")
    ap.add_argument("--games", type=int, default=20)
    ap.add_argument("--size", type=int, default=9)
    ap.add_argument("--komi", type=float, default=7.5)
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--seed", type=int, default=None,
                    help="base RNG seed; game g uses seed+g for both engines, "
                         "making the match reproducible and independent of "
                         "wall-clock time. Omit to keep each engine's default "
                         "(time-based) seeding.")
    args = ap.parse_args()

    # Track wins for engine A regardless of which color it played.
    a_wins = b_wins = draws = 0
    t0 = time.time()

    for g in range(args.games):
        a_is_black = (g % 2 == 0)  # alternate colors for fairness
        black = GtpEngine(args.black if a_is_black else args.white)
        white = GtpEngine(args.white if a_is_black else args.black)
        game_seed = None if args.seed is None else args.seed + g
        try:
            winner = play_game(black, white, args.size, args.komi, game_seed)
        finally:
            black.close()
            white.close()

        if winner is None:
            draws += 1
            tag = "draw"
        else:
            a_won = (winner == "B") == a_is_black
            if a_won:
                a_wins += 1
            else:
                b_wins += 1
            tag = "A" if a_won else "B"
        if args.verbose:
            color = "black" if a_is_black else "white"
            print(f"game {g + 1}/{args.games}: A played {color}, "
                  f"winner={winner} -> {tag}", flush=True)

    decided = a_wins + b_wins
    elo, band = elo_diff(a_wins, decided)
    dt = time.time() - t0
    print("-" * 56)
    print(f"A: {args.black}")
    print(f"B: {args.white}")
    print(f"games={args.games}  A_wins={a_wins}  B_wins={b_wins}  draws={draws}")
    if decided:
        print(f"A win rate: {100.0 * a_wins / decided:.1f}%")
        print(f"A Elo vs B: {elo:+.0f} +/- {band:.0f}")
    print(f"elapsed: {dt:.1f}s ({dt / max(args.games,1):.1f}s/game)")


if __name__ == "__main__":
    sys.exit(main())
