#!/usr/bin/env python3
"""Emit KataGo's best move (search-improved policy argmax) + the position:
   <kata_r> <kata_c> <ctm> <nstones> <r c col>...   (kata_r=-1 => pass)"""
import numpy as np, sys
z = np.load(sys.argv[1]); obs, pol = z['obs'], z['policy']
N = obs.shape[0]
idx = np.random.RandomState(7).permutation(N)[:int(sys.argv[3])]
with open(sys.argv[2], 'w') as f:
    for i in idx:
        o = obs[i]; cur = o[:,:,0]>0.5; opp = o[:,:,1]>0.5
        btm = o[:,:,16].mean() > 0.5   # calibration-verified polarity (see decode_positions.py)
        ctm = 'B' if btm else 'W'; oc = 'W' if btm else 'B'
        am = int(pol[i].argmax())
        kr, kc = (-1,-1) if am==81 else (am//9, am%9)
        st = []
        for r in range(9):
            for c in range(9):
                if cur[r,c]: st.append((r,c,ctm))
                elif opp[r,c]: st.append((r,c,oc))
        f.write("%d %d %s %d " % (kr,kc,ctm,len(st)))
        f.write(" ".join("%d %d %s"%s for s in st)); f.write("\n")
print("wrote", len(idx), "positions")
