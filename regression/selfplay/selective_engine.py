#!/usr/bin/env python3
"""Selective-search GTP proxy: a one-ply global lookahead over GNU Go.

GNU Go picks its move from a *static* evaluation of move reasons -- it has
almost no whole-board lookahead. This proxy adds the missing lookahead without
touching the engine: it speaks GTP to the harness, wraps a real gnugo process,
and intercepts `genmove`. For a genmove it asks gnugo for its own ranked
candidate moves (`top_moves_<color>`), then for each candidate actually plays
it and reads gnugo's whole-board score estimate (`estimate_score`), undoes it,
and finally plays the candidate that leaves the best resulting position. Every
other GTP command is forwarded verbatim, so board state stays consistent.

This is the "global selective search" idea (Step 4) in its simplest sound
form: reuse GNU Go's own candidate generator and its own positional evaluation
as the leaf, and choose by the value of the resulting position rather than by
the static move-reason value alone. It is measured against plain gnugo with
twogtp.py; whether 1-ply re-ranking helps is an empirical, tunable question.

Usage (as an engine for twogtp.py):
    python3 selective_engine.py --gnugo "../../interface/gnugo --mode gtp --level 1" --width 5
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from twogtp import GtpEngine


def parse_score(resp):
    """'W+3.8 (...)' / 'B+5.5 (...)' -> signed score, + favors White."""
    m = re.match(r"\s*([BW])\+([0-9.]+)", resp)
    if not m:
        return 0.0
    val = float(m.group(2))
    return val if m.group(1) == "W" else -val


def choose_move(inner, color, width):
    """Return the selected vertex (or None to fall back to plain genmove)."""
    cand = inner.send(f"top_moves_{color}").split()
    # top_moves returns "VERTEX VALUE VERTEX VALUE ...".
    moves = []
    for i in range(0, len(cand) - 1, 2):
        try:
            moves.append((cand[i], float(cand[i + 1])))
        except ValueError:
            break
    moves = [m for m, v in moves[:width] if v > 0.0]
    if not moves:
        return None

    sign = 1.0 if color == "white" else -1.0
    best_move, best_score = None, None
    for m in moves:
        inner.send(f"play {color} {m}")
        score = parse_score(inner.send("estimate_score")) * sign
        inner.send("undo")
        if best_score is None or score > best_score:
            best_score, best_move = score, m
    return best_move


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gnugo", required=True, help="inner gnugo GTP command")
    ap.add_argument("--width", type=int, default=5, help="candidates to look at")
    args = ap.parse_args()

    inner = GtpEngine(args.gnugo)

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        m = re.match(r"^(\d+)?\s*(.*)$", line)
        cmd_id, body = m.group(1), m.group(2)
        prefix = f"={cmd_id}" if cmd_id else "="

        gm = re.match(r"genmove\s+(black|white|b|w)\s*$", body, re.I)
        if gm:
            color = {"b": "black", "w": "white"}.get(gm.group(1).lower(),
                                                     gm.group(1).lower())
            try:
                move = choose_move(inner, color, args.width)
            except Exception:
                move = None
            if move is None:
                move = inner.send(f"genmove {color}")  # fall back
            else:
                inner.send(f"play {color} {move}")     # commit on inner engine
            sys.stdout.write(f"{prefix} {move}\n\n")
            sys.stdout.flush()
            continue

        if body == "quit":
            try:
                inner.close()
            except Exception:
                pass
            sys.stdout.write(f"{prefix}\n\n")
            sys.stdout.flush()
            break

        # Forward everything else verbatim.
        try:
            resp = inner.send(body)
            sys.stdout.write(f"{prefix} {resp}\n\n")
        except Exception as e:
            sys.stdout.write(f"?{cmd_id or ''} {e}\n\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
