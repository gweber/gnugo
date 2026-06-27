#!/bin/sh
# Profile-Guided Optimization build for GNU Go's Monte-Carlo engine.
#
# The MC playout is a branchy hot loop, the classic case PGO helps.  HONESTY
# CAVEAT: on the machine this was developed on (a loaded big.LITTLE box, and a
# RANDOMIZED playout workload) the effect could NOT be measured reliably -- the
# run-to-run cycle noise (~3-6%) swamped any PGO/-O3 vs stock -O2 difference, and
# -O2 measured as fast as anything.  So treat this as a sound-but-unproven build
# option, not a guaranteed win; verify with `perf stat -e cycles` on a QUIET
# machine and a FIXED position before relying on it.  No source change, identical
# strength either way.  Set LTO=1 to also try link-time optimization.
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
