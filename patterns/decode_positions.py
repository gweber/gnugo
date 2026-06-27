#!/usr/bin/env python3
"""Decode KataGo-labeled 9x9 positions (mushin-go distill_*.npz) into a flat text
file the C tools can replay:  one line per position
    <target> <ctm> <nstones> <r c col> <r c col> ...
target = P(side-to-move wins) = (value+1)/2 ; ctm/col in {B,W}.
obs plane 0 = side-to-move stones, plane 1 = opponent, plane 16 = side-to-move
colour flag.  VERIFIED against the data (move parity = plane0-plane1 stone diff):
plane16==1 <=> WHITE to move, plane16==0 <=> BLACK to move.  The original default
had this INVERTED (treated plane16>0.5 as black) -- corrected below.  If you ever
passed the "flip" arg to fix it, DROP it now (the default is correct).
"""
import numpy as np
import sys

npz, out, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
flip = len(sys.argv) > 4 and sys.argv[4] == "flip"   # plane16 polarity

z = np.load(npz)
obs, val = z["obs"], z["value"]
N = obs.shape[0]
idx = np.random.RandomState(1).permutation(N)[:n]

with open(out, "w") as f:
    for i in idx:
        o = obs[i]
        cur = o[:, :, 0] > 0.5
        opp = o[:, :, 1] > 0.5
        black_to_move = (o[:, :, 16].mean() < 0.5) ^ flip   # plane16==1 => WHITE
        ctm = "B" if black_to_move else "W"
        oc = "W" if black_to_move else "B"
        target = (float(val[i]) + 1.0) / 2.0
        stones = []
        for r in range(9):
            for c in range(9):
                if cur[r, c]:
                    stones.append((r, c, ctm))
                elif opp[r, c]:
                    stones.append((r, c, oc))
        f.write("%.4f %s %d " % (target, ctm, len(stones)))
        f.write(" ".join("%d %d %s" % s for s in stones))
        f.write("\n")
print("wrote %d positions to %s" % (len(idx), out))
