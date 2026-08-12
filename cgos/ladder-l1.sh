#!/usr/bin/env bash
# Ladder rung ~1950-2100 (estimate): book binary, single thread, FIXED
# level 1 -- the cheapest MC rung.  Weight pins as in the main wrapper.
cd "$(dirname "$0")"
exec env \
  GNUGO_RAVE=1 \
  GNUGO_MC_AVOID_SELFATARI=1 \
  GNUGO_MC_LCB=1.28 \
  GNUGO_TERRITORIAL_WEIGHT=1.0 GNUGO_STRATEGICAL_WEIGHT=1.0 \
  GNUGO_ATTACK_DRAGON_WEIGHT=1.0 GNUGO_FOLLOWUP_WEIGHT=1.0 \
  GNUGO_INVASION_MALUS_WEIGHT=1.0 GNUGO_SHAPE_FACTOR_BASE=1.05 \
  GNUGO_LUNCH_MULTIPLIER=1.8 \
  ./gnugo-3.9.1b-bin --mode gtp --monte-carlo \
    --chinese-rules --positional-superko --capture-all-dead \
    --level 1 --min-level 1 --max-level 1
