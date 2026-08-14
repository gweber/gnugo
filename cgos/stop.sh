#!/usr/bin/env bash
# Graceful stop: the client finishes the current rated game, then exits, and
# the runner loop sees kill-<name>.txt (which the client never touches) and
# stops.  Usage:
#   ./stop.sh                 # stop the main bot (saigo.gnugo3.9.1b)
#   ./stop.sh <cgos-name>     # stop one ladder bot
#   ./stop.sh all             # stop every bot run from this directory
cd "$(dirname "$0")"
stop_one() {
  touch "kill-$1.txt" "kill-client-$1.txt"
  # Legacy paths: clients started before the per-name kill files existed
  # watch these instead.  A stale client silently ignoring the new files
  # once left the bot running an outdated config for 32 hours -- always
  # verify with `pgrep -af cgosGtp` which -k path the live client watches.
  touch kill.txt kill-client.txt
  echo "[cgos] stop requested for $1; it exits after the current game."
}
case "${1:-saigo.gnugo3.9.1b}" in
  all)
    for f in kill-client-*.txt; do :; done   # no-op if none exist yet
    for n in saigo.gnugo3.9.1b saigo.gnugo3.9.1 saigo.gnugo-l3 \
	     saigo.gnugo-l1 saigo.gnugoclassic; do
      stop_one "$n"
    done
    ;;
  *)
    stop_one "${1:-saigo.gnugo3.9.1b}"
    ;;
esac
