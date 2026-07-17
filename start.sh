#!/bin/sh
# Authenticate once, then start (or restart) the lightbar daemon detached.
# Run this at login; the daemon keeps running after the terminal closes.
set -eu

here="$(cd "$(dirname "$0")" && pwd)"
bin="$here/build/claude-light"
log=/tmp/claude-light.log

[ -x "$bin" ] || make -C "$here" ${PIXEL_LIGHTS:+PIXEL_LIGHTS="$PIXEL_LIGHTS"} >/dev/null

sudo -v                                          # ask for the password once, cache it
sudo pkill -f 'claude-light daemon' 2>/dev/null || true
sudo -n setsid "$bin" daemon >"$log" 2>&1 &      # detached, off the terminal

sleep 1
if pgrep -f 'claude-light daemon' >/dev/null; then
    echo "claude-light daemon running (log: $log)"
else
    echo "start failed; $log says:"
    cat "$log"
    exit 1
fi
