#!/usr/bin/env bash
# Ladder rung ~1850-1950 (estimate): the CLASSICAL engine at level 10 with
# the SPSA-tuned weights (the binary defaults, 5113bb63).  Against the
# Gnugo-3.7.10-a1 anchor (1800) this externally measures the classical
# fork's gain, weights included.  Single core, sub-second moves.
cd "$(dirname "$0")"
exec ./gnugo-3.9.1b-bin --mode gtp \
  --chinese-rules --positional-superko --capture-all-dead \
  --level 10
