#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
build=${1:-"$root/build/linux"}
stage="$root/build/stage/linux-$(date +%Y%m%d-%H%M%S)-$$"
mkdir -p "$root/dist"
cmake --install "$build" --prefix "$stage"
test -x "$stage/SecondRealityRemix"
test -f "$stage/assets/music/luminous-flow.wav"
(cd /tmp && "$stage/SecondRealityRemix" --time 42 --duration 1 --hidden --no-audio)
tar -czf "$root/dist/SecondRealityRemix-Linux-x64.tar.gz" -C "$stage" .
sha256sum "$root/dist/SecondRealityRemix-Linux-x64.tar.gz"
printf 'STAGE: %s\n' "$stage"
