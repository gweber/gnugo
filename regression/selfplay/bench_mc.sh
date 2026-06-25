#!/bin/bash
# bench_mc.sh -- single-thread throughput benchmark for the Monte Carlo engine.
#
# Times a fixed batch of MC genmoves from a fixed mid-game position (genmove +
# undo, so every search runs from the same position) with a fixed RNG seed, so
# the workload is deterministic and reproducible across builds. Lower time =
# higher playout throughput. Use it to measure compiler-flag / code changes.
#
# Usage: ./bench_mc.sh [size] [level] [n_genmoves]
#   GNUGO=/path/to/gnugo ./bench_mc.sh 9 1 30
set -e
GNUGO=${GNUGO:-../../interface/gnugo}
SIZE=${1:-9}
LEVEL=${2:-1}
N=${3:-30}
GTP=$(mktemp)

{
  echo "boardsize $SIZE"
  echo "clear_board"
  echo "komi 7.5"
  echo "set_random_seed 1"
  # a contested mid-game position, where playouts do real work
  echo "play black E5"; echo "play white C3"; echo "play black G5"
  echo "play white C6"; echo "play black E3"; echo "play white G7"
  echo "play black C5"; echo "play white E7"; echo "play black E6"
  i=0
  while [ "$i" -lt "$N" ]; do
    echo "genmove black"
    echo "undo"
    i=$((i + 1))
  done
  echo "quit"
} > "$GTP"

echo "workload: $N MC genmoves, size $SIZE level $LEVEL (= $((8000 * LEVEL)) sims each)"
start=$(date +%s.%N)
env GNUGO_RAVE=1 GNUGO_MC_AVOID_SELFATARI=1 "$GNUGO" \
    --mode gtp --monte-carlo --level "$LEVEL" < "$GTP" >/dev/null 2>&1
end=$(date +%s.%N)
rm -f "$GTP"
t=$(awk "BEGIN{printf \"%.3f\", $end-$start}")
echo "time: ${t}s  (=> $(awk "BEGIN{printf \"%.0f\", $N*8000*$LEVEL/$t}") sims/s)"
