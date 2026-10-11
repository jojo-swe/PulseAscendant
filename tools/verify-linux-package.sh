#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
build=${1:-"$root/build/linux"}
mkdir -p "$root/build/extracted"
stage=$(mktemp -d "$root/build/extracted/linux-XXXXXXXX")
tar -xzf "${2:-$root/dist/three-acts}/PulseAscendant-Linux-x64.tar.gz" -C "$stage"
cmp "$stage/PulseAscendant" "$build/PulseAscendant"
cmp "$stage/assets/music/second-reality-three-acts.wav" "$root/assets/music/second-reality-three-acts.wav"
for show_time in 42 342 620; do
 (cd /tmp && "$stage/PulseAscendant" --time "$show_time" --duration 1 --hidden --no-audio --width 640 --height 360)
done
printf 'EXTRACTED LINUX TARBALL PASS\n'
