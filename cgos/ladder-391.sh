#!/usr/bin/env bash
# Ladder rung: REVIVE saigo.gnugo3.9.1 with exactly the config that earned
# its ~2334 rating (pre-book snapshot binary, 4 tree threads, LCB, autolevel
# 2-12, no pondering).  Keeps that rating line alive as the fixed mid-anchor
# the 9x9 room lacks -- and the gap to saigo.gnugo3.9.1b externally measures
# book+ponder+8-threads+early-stop.
cd "$(dirname "$0")"
exec env \
  GNUGO_RAVE=1 \
  GNUGO_MC_AVOID_SELFATARI=1 \
  GNUGO_MC_LCB=1.28 \
  GNUGO_MC_TREE_THREADS=4 \
  ./gnugo-3.9.1-bin --mode gtp --monte-carlo \
    --chinese-rules --positional-superko --capture-all-dead \
    --level 8 --min-level 2 --max-level 12
