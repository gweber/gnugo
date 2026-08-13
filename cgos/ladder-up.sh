#!/usr/bin/env bash
# Start the rating-ladder rungs, one tmux session each.  The 9x9 room has
# almost no players between ~2150 and ~2700 -- exactly where our main bot
# lives -- so these rungs populate the range with known quantities (and
# double as external measurements: 3.9.1 vs 3.9.1b prices book+ponder+
# threads; classic vs the 1800 anchor prices the classical fork).
cd "$(dirname "$0")"
start() {  # name wrapper
  local session="cgos_${1//[.\-]/_}"
  tmux has-session -t "$session" 2>/dev/null && {
    echo "[ladder] $1 already running"; return; }
  tmux new-session -d -s "$session" \
    "cd $(pwd) && CGOS_NAME=$1 CGOS_WRAP=$(pwd)/$2 ./run-cgos.sh 2>&1 | tee -a ladder-$1.log"
  echo "[ladder] started $1 ($2) in tmux $session"
}
# NOTE: CGOS rejects names longer than 18 characters (the server answers
# "User name must be no more than 18 characters long." and the client then
# reconnect-loops forever).  Keep every name short.
start saigo.gnugo3.9.1      ladder-391.sh
start saigo.gnugo-l3        ladder-l3.sh
start saigo.gnugo-l1        ladder-l1.sh
start saigo.gnugoclassic    ladder-classic.sh
