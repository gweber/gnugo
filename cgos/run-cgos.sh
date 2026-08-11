#!/usr/bin/env bash
# Put this fork's gnugo on CGOS 9x9 as saigo.gnugo3.9.1.
#
# Modeled on the saigo/katago runners: creds are SOURCED at runtime (never
# stored here), the cfg carrying the password lives in /tmp mode-600 and is
# removed on exit, and `touch kill.txt` stops gracefully (the client finishes
# the current game, then exits -- no mid-game disconnect, no rating damage).
#
#   ./run-cgos.sh                     # foreground
#   tmux new-session -d -s cgos_gnugo ./run-cgos.sh   # how it's deployed
set -uo pipefail
cd "$(dirname "$0")"
CREDS="${CGOS_CREDS:-/home/taro/code/archive/mushin-go/training-jax/.cgos_env}"
[ -f "$CREDS" ] && source "$CREDS"
: "${CGOS_PASSWORD:?CGOS_PASSWORD not set (source $CREDS)}"
NAME="${CGOS_NAME:-saigo.gnugo3.9.1}"
SERVER="${CGOS_SERVER:-yss-aya.com}"; PORT="${CGOS_PORT:-6809}"
WRAP="$(pwd)/gnugo-cgos9-gtp.sh"
CLIENT=/home/taro/code/CGOS/client/cgosGtp.tcl
KILL="$(pwd)/kill.txt"; rm -f "$KILL"
CFG="$(mktemp /tmp/cgos_gnugo_XXXXXX.cfg)"; chmod 600 "$CFG"; trap 'rm -f "$CFG"' EXIT
cat > "$CFG" <<CFGEOF
%section server
    server $SERVER
    port $PORT
%section player
    name $NAME
    password $CGOS_PASSWORD
    invoke $WRAP
    priority 7
CFGEOF
echo "[cgos] $NAME -> $SERVER:$PORT  (stop: touch $KILL)"
while [ ! -f "$KILL" ]; do
  tclsh8.6 "$CLIENT" -c "$CFG" -k "$KILL"
  [ -f "$KILL" ] && break
  echo "[cgos] client dropped; reconnecting in 15s"; sleep 15
done
echo "[cgos] stopped."
