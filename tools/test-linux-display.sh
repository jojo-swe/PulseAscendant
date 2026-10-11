#!/usr/bin/env bash
# Run native window lifecycle checks on an isolated X11 desktop.
# Requires Xvfb, openbox and xprop (Ubuntu: xvfb openbox x11-utils).
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
build=${1:-"$root/build/linux"}
mkdir -p "$root/build/integration"
run=$(mktemp -d "$root/build/integration/x11-XXXXXXXX")
xvfb_pid= wm_pid=
cleanup() {
  if [[ -n "$wm_pid" ]]; then kill "$wm_pid" 2>/dev/null || true; fi
  if [[ -n "$xvfb_pid" ]]; then kill "$xvfb_pid" 2>/dev/null || true; fi
}
trap cleanup EXIT
Xvfb -displayfd 3 -screen 0 1280x720x24 -nolisten tcp -ac 3>"$run/display" >"$run/xvfb.log" 2>&1 &
xvfb_pid=$!
for ((i=0;i<100;i++)); do
  [[ -s "$run/display" ]] && break
  kill -0 "$xvfb_pid"
  sleep .05
done
[[ -s "$run/display" ]] || { cat "$run/xvfb.log"; exit 1; }
export DISPLAY=":$(cat "$run/display")" SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
export XDG_CONFIG_HOME="$run/config" XDG_CACHE_HOME="$run/cache"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME"
openbox --sm-disable >"$run/openbox.log" 2>&1 &
wm_pid=$!
for ((i=0;i<100;i++)); do
  if xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; then break; fi
  kill -0 "$wm_pid"
  sleep .05
done
xprop -root _NET_SUPPORTING_WM_CHECK
ctest --test-dir "$build" --output-on-failure
