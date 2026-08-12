#!/usr/bin/env bash
# Graceful stop: the client finishes the current rated game, then exits, and
# the runner loop sees kill.txt (which the client never touches) and stops.
cd "$(dirname "$0")"
touch kill.txt kill-client.txt
echo "[cgos] stop requested; bot exits after the current game."
