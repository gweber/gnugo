#!/bin/sh
# Build the standalone MC research tools: mccalib, mcdiff, mcpredict, mctrain.
#
# These are deliberately NOT in the autotools build: mccalib and mcdiff link
# against saigo's L&D oracle (saigo_ownership_ld; the oracle was originally
# tugo, long since merged into saigo), which lives out of tree.  Prerequisites:
#
#   1. a built gnugo tree:   ./configure && make      (for the .a libraries)
#   2. the saigo oracle:     cargo build --release -p saigo-net \
#                              --manifest-path "$SAIGO/Cargo.toml"
#
# Override SAIGO to point elsewhere:  SAIGO=/path/to/saigo ./build-mctools.sh
set -e
cd "$(dirname "$0")"

SAIGO=${SAIGO:-/home/taro/code/saigo.online/engines/saigo}
ORACLE="$SAIGO/target/release/libsaigo_net.so"
# Same order as interface/Makefile.am LDADD; ncurses for gg_utils' terminfo use.
LIBS="../engine/libengine.a libpatterns.a ../sgf/libsgf.a ../utils/libutils.a -lncurses"
CFLAGS="-O2 -g -I.. -I. -I../engine -I../sgf -I../utils"

if [ ! -f ../engine/libengine.a ]; then
  echo "error: build gnugo first (./configure && make)" >&2
  exit 1
fi
if [ ! -f "$ORACLE" ]; then
  echo "error: oracle library not found: $ORACLE" >&2
  echo "build it: cargo build --release -p saigo-net --manifest-path $SAIGO/Cargo.toml" >&2
  exit 1
fi

# -Wl,-rpath so the binaries find libsaigo_net.so without LD_LIBRARY_PATH.
cc $CFLAGS mcdiff.c    $LIBS "$ORACLE" -Wl,-rpath,"$(dirname "$ORACLE")" -lm -o mcdiff
cc $CFLAGS mccalib.c   $LIBS "$ORACLE" -Wl,-rpath,"$(dirname "$ORACLE")" -lm -o mccalib
cc $CFLAGS mcpredict.c $LIBS -lm -o mcpredict
cc $CFLAGS mctrain.c   $LIBS -lm -o mctrain
echo "built: mcdiff mccalib mcpredict mctrain"
