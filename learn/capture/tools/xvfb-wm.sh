#!/usr/bin/env bash
# Runs a command on a private Xvfb display with a minimal window manager (openbox).
# Measured in Gate 0/1: JUCE's real-window path needs both `-noreset` (Xvfb otherwise drops
# the window-manager atoms when the last client disconnects) and a running window manager.
#   learn/capture/tools/xvfb-wm.sh <command> [args...]
set -euo pipefail
size="${LEARN_XVFB_SCREEN:-3200x2400x24}"
exec xvfb-run -a -s "-noreset -screen 0 ${size}" bash -c '
  openbox >/dev/null 2>&1 &
  wm=$!
  # wait until the window manager owns the screen (its _NET_SUPPORTING_WM_CHECK appears)
  for i in $(seq 1 50); do
    if xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q window; then break; fi
    sleep 0.1
  done
  "$@"; rc=$?
  kill $wm 2>/dev/null || true
  exit $rc' bash "$@"
