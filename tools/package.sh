#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
build=${1:-"$root/build/linux"}
stage="$root/build/stage/linux-$(date +%Y%m%d-%H%M%S)-$$"
dist=${2:-"$root/dist/three-acts"}
mkdir -p "$dist"
cmake --install "$build" --prefix "$stage"
test -x "$stage/PulseAscendant"
test -f "$stage/assets/music/second-reality-three-acts.wav"
for show_time in 42 342 620; do
 (cd /tmp && "$stage/PulseAscendant" --time "$show_time" --duration 1 --hidden --no-audio --width 640 --height 360)
done
tar -czf "$dist/PulseAscendant-Linux-x64.tar.gz" --mode=u+rwX,go+rX,go-w -C "$stage" .
sha256sum "$dist/PulseAscendant-Linux-x64.tar.gz"
printf 'STAGE: %s\n' "$stage"
