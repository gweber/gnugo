#!/usr/bin/env bash
# GTP wrapper for the CGOS 9x9 bot saigo.gnugo3.9.1b (b = KataGo-derived
# fuseki book, +60 +/- 38 SPRT-measured; commit a3c96ced).
#
# Runs the SNAPSHOTTED binary (gnugo-3.9.1b-bin, copied at deploy time), never
# interface/gnugo directly: in-tree rebuilds must not swap the engine under a
# live rated game.  Re-snapshot deliberately with:
#   cp ../interface/gnugo gnugo-3.9.1b-bin   (while the bot is between games)
#
# Config: the measured-strongest NON-experimental MC stack --
#   tree-parallel 4 threads (+237 over root-par), tuned RAVE (+92),
#   self-atari avoidance (+31), LCB root selection (+19), KataGo book
#   (+60), pondering on the opponent's clock (+72 local proxy; implies
#   tree reuse).  On CGOS the opponent thinks on THEIR hardware, so the
#   pondered sims are genuinely free compute.
# Time management: gnugo's GTP genmove path runs adjust_level_offset()
# unconditionally, fed by the time_settings/time_left the CGOS client sends,
# and the MC sims budget scales with get_level() EVERY move -- so the level
# range below is a semi-dynamic clock schedule (spend ~ time_left /
# (stones_left+4.5), front-loaded, decaying).  Measured under full box load,
# 4 threads: level 8 ~3.1 s/move, level 12 ~7 s/move peak burn, level 2
# ~1 s/move endgame scramble; equilibrium spends the 300 s absolute clock
# without flagging.  Start 8, ramp within [2, 12].
# CGOS is Tromp-Taylor: chinese rules, positional superko, and
# --capture-all-dead so dead stones are physically removed before scoring.
# Weight pins: the binary's DEFAULT valuation weights are the SPSA optimum
# for the CLASSICAL engine (+92/13x13, +43/19x19) -- but under the 9x9 MC
# stack the same weights measured mildly NEGATIVE (potential_moves feeds the
# search a different balance than big-board classical play wants), so the
# bot pins the historical values.
# 8 tree threads: measured 4.25x single-thread sims/sec under full box load
# (4 threads: 2.75x); autolevel converts the throughput into higher levels
# on the same clock, hence max-level 14 (was 12).  Early stop at margin 1.0
# is loss-free by construction and banks clock for later moves.
cd "$(dirname "$0")"
exec env \
  GNUGO_RAVE=1 \
  GNUGO_MC_AVOID_SELFATARI=1 \
  GNUGO_MC_LCB=1.28 \
  GNUGO_MC_TREE_THREADS=8 \
  GNUGO_MC_PONDER=1 \
  GNUGO_MC_EARLYSTOP=1.0 \
  GNUGO_MC_SCOREUTIL=0.003 \
  GNUGO_MC_UNST=0.5 \
  GNUGO_MC_CLASSICAL_ROOT=0.15 \
  GNUGO_MC_NO_OVERRIDE=1 \
  GNUGO_TERRITORIAL_WEIGHT=1.0 \
  GNUGO_STRATEGICAL_WEIGHT=1.0 \
  GNUGO_ATTACK_DRAGON_WEIGHT=1.0 \
  GNUGO_FOLLOWUP_WEIGHT=1.0 \
  GNUGO_INVASION_MALUS_WEIGHT=1.0 \
  GNUGO_SHAPE_FACTOR_BASE=1.05 \
  GNUGO_LUNCH_MULTIPLIER=1.8 \
  ./gnugo-3.9.1b-bin --mode gtp --monte-carlo \
    --chinese-rules --positional-superko --capture-all-dead \
    --level 8 --min-level 2 --max-level 14
