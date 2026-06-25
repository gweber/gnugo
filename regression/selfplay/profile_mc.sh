#!/bin/sh
# profile_mc.sh -- CPU profile of the Monte Carlo / UCT playout engine.
#
# Finds where the MC engine spends cycles, to guide playout/throughput
# optimization (more simulations = stronger, and the precondition for lifting
# the 9x9 cap).
#
# Uses perf's software task-clock event rather than hardware cycles: on this
# (virtualized ARM) box the hardware PMU is multiplexed and drops almost all
# samples, whereas task-clock is time-based, always available, and for
# CPU-bound code is proportional to cycles -- same bottleneck answer.
#
# Usage: ./profile_mc.sh [level] [rounds]
set -e
LEVEL=${1:-2}
ROUNDS=${2:-10}
GNUGO=../../interface/gnugo
OUT=$(mktemp -d)
GTP="$OUT/load.gtp"

# Sustained workload: ROUNDS x (clear_board + 18 genmoves) so playouts keep
# doing real work instead of passing on a full board.
{
  echo "boardsize 9"
  r=0; while [ "$r" -lt "$ROUNDS" ]; do
    echo "clear_board"
    i=0; while [ "$i" -lt 9 ]; do echo "genmove black"; echo "genmove white"; i=$((i+1)); done
    r=$((r+1))
  done
  echo "quit"
} > "$GTP"

echo "workload: $(grep -c genmove "$GTP") MC genmoves at level $LEVEL"
perf record -e task-clock -F 4000 -o "$OUT/perf.data" -- \
  env GNUGO_RAVE=1 "$GNUGO" --mode gtp --monte-carlo --level "$LEVEL" \
  < "$GTP" >/dev/null 2>/dev/null

echo "=== samples ==="
perf report -i "$OUT/perf.data" --stdio 2>/dev/null | grep -i "# Samples:" | head -1
echo "=== top self-time functions ==="
perf report -i "$OUT/perf.data" --stdio --percent-limit 1 2>/dev/null \
  | grep -E "^\s+[0-9]+\.[0-9]+%\s+gnugo" | head -15
echo
echo "perf.data: $OUT/perf.data  (perf annotate -i ... <func> for line detail)"
