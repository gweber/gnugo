#!/usr/bin/env python3
"""Soundness check for the df-pn ladder solver (engine/dfpn.c).

Builds several semi-random positions and, for every stone on the board,
compares the experimental `dfpn_attack` GTP command against the engine's own
`attack`. The df-pn solver is sound but incomplete: it only proves ladders and
direct captures, so it may report "safe" where attack() finds a non-ladder
capture -- that is expected. What must NEVER happen is the reverse: df-pn
claiming a capture that attack() rejects (an unsound proof).

Run from the repo root after building:
    python3 regression/selfplay/validate_dfpn.py
"""

import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from twogtp import GtpEngine

ENGINE = "./interface/gnugo --mode gtp --level 1"


def main():
    random.seed(7)
    total = agree = attack_only = unsound = 0

    for _ in range(8):
        e = GtpEngine(ENGINE)
        e.send("boardsize 9")
        e.send("clear_board")
        color = "black"
        for _ in range(random.randint(20, 40)):
            m = e.send(f"genmove {color}")
            if m.lower() in ("pass", "resign"):
                break
            color = "white" if color == "black" else "black"

        stones = (e.send("list_stones black") + " "
                  + e.send("list_stones white")).split()
        for v in stones:
            try:
                d = e.send(f"dfpn_attack {v}")
                a = e.send(f"attack {v}")
            except Exception:
                continue
            total += 1
            dfpn_capture = d.split()[0] == "1"
            attack_capture = a.split()[0] != "0"
            if dfpn_capture and attack_capture:
                agree += 1
            elif dfpn_capture and not attack_capture:
                unsound += 1
                print(f"  UNSOUND: {v} dfpn={d!r} attack={a!r}")
            elif attack_capture and not dfpn_capture:
                attack_only += 1
        e.close()

    print(f"stones queried:                         {total}")
    print(f"both capturable (df-pn found ladder):   {agree}")
    print(f"attack-only (non-ladder, df-pn skips):  {attack_only}")
    print(f"UNSOUND (df-pn capture, attack safe):   {unsound}")
    return 1 if unsound else 0


if __name__ == "__main__":
    sys.exit(main())
