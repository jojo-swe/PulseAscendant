#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
build=${1:-"$root/build/linux"}
mkdir -p "$root/build/extracted"
stage=$(mktemp -d "$root/build/extracted/linux-XXXXXXXX")
tar -xzf "$root/dist/SecondRealityRemix-Linux-x64.tar.gz" -C "$stage"
cmp "$stage/SecondRealityRemix" "$build/SecondRealityRemix"
cmp "$stage/assets/music/luminous-flow.wav" "$root/assets/music/luminous-flow.wav"
(cd /tmp && "$stage/SecondRealityRemix" --time 42 --duration 1 --hidden --no-audio)
printf 'EXTRACTED LINUX TARBALL PASS\n'
