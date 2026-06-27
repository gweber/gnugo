#!/bin/sh
# Profile-Guided Optimization build for GNU Go's Monte-Carlo engine.
#
# The MC playout is a branchy hot loop; PGO lays out its branches/inlining from
# a real run and buys ~4% fewer cycles (measured) = ~4% more simulations at fixed
# wall-time, with no source change and identical strength.  Set LTO=1 to also try
# link-time optimization (cross-file inlining of the board primitives).
#
#   ./pgo-build.sh          # PGO only
#   LTO=1 ./pgo-build.sh    # PGO + LTO
#
# Run from the source root.  Leaves interface/gnugo as the optimized binary and
# removes the .gcda/.gcno profile artifacts.

set -e
cd "$(dirname "$0")"

ARCH="-O3 -mcpu=native -pthread"
[ -n "$LTO" ] && ARCH="$ARCH -flto"
GEN="$ARCH -fprofile-generate -fprofile-update=atomic"
USE="$ARCH -fprofile-use -fprofile-correction -Wno-coverage-mismatch"
JOBS="${JOBS:-8}"

# A fixed profiling workload: Monte-Carlo genmoves from non-empty positions (so the
# fuseki shortcut doesn't skip the search), single- and multi-threaded, to cover the
# playout, the move-value maintenance, and the tree-parallel descent.
WORK=$(mktemp)
{
  echo "boardsize 9"
  i=0
  while [ $i -lt 4 ]; do
    echo "clear_board"
    echo "play black E5"; echo "play white C3"; echo "play black G5"
    echo "play white E3"; echo "play black C7"; echo "play white G7"
    echo "play black C5"; echo "play white E7"; echo "play black G3"
    echo "genmove white"
    i=$((i + 1))
  done
  echo "quit"
} > "$WORK"

RUN="env GNUGO_RAVE=1 GNUGO_MC_AVOID_SELFATARI=1"

echo "[pgo] 1/3 instrumented build ($GEN)"
make clean >/dev/null 2>&1 || true
make -j"$JOBS" CFLAGS="$GEN" >/dev/null

echo "[pgo] 2/3 profiling run (single + tree-parallel)"
$RUN ./interface/gnugo --quiet --mode gtp --monte-carlo --level 3 < "$WORK" >/dev/null 2>&1
env GNUGO_RAVE=1 GNUGO_MC_AVOID_SELFATARI=1 GNUGO_MC_TREE_THREADS=4 \
    ./interface/gnugo --quiet --mode gtp --monte-carlo --level 3 < "$WORK" >/dev/null 2>&1

echo "[pgo] 3/3 optimized rebuild ($USE)"
find . -name '*.o' -delete
make -j"$JOBS" CFLAGS="$USE" >/dev/null

find . -name '*.gcda' -delete
find . -name '*.gcno' -delete
rm -f "$WORK"
echo "[pgo] done -> interface/gnugo"
