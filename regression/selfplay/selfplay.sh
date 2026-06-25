#!/bin/sh
# selfplay.sh -- convenience wrapper around twogtp.py.
#
# Measures the playing strength of one gnugo build/config (A) against another
# (B) by self-play, reporting an Elo difference.
#
# Usage:
#   ./selfplay.sh "<A gtp cmd>" "<B gtp cmd>" [games] [size] [komi]
#
# Examples:
#   # current build vs a pinned reference build, 40 games on 9x9
#   ./selfplay.sh \
#     "../../interface/gnugo --mode gtp --level 1" \
#     "/opt/gnugo-ref/gnugo --mode gtp --level 1" 40 9 7.5
#
#   # A/B test two parameter sets of the SAME binary via env overrides
#   GG=../../interface/gnugo
#   ./selfplay.sh \
#     "env GNUGO_TERRITORIAL_WEIGHT=1.2 $GG --mode gtp --level 1" \
#     "$GG --mode gtp --level 1" 100 9
set -e
A=${1:?need engine A command}
B=${2:?need engine B command}
GAMES=${3:-20}
SIZE=${4:-9}
KOMI=${5:-7.5}
DIR=$(dirname "$0")
exec python3 "$DIR/twogtp.py" \
    --black "$A" --white "$B" \
    --games "$GAMES" --size "$SIZE" --komi "$KOMI" --verbose
