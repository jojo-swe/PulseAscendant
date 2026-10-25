#!/bin/sh
# Renders EVOLUTION at 3840x2160, 60 fps and 64 samples per pixel into dist/render/EVOLUTION-2160p60.mp4 (H.264
# High, BT.709, AAC 384 kb/s): configures and builds the Release preset, checks for ffmpeg, then runs
# tools/render_video.py, which renders in 10-second chunks. Interrupted? Run the same command again: finished
# chunks are kept and the render carries on from the first missing one.
#
#   tools/render_4k.sh
#   tools/render_4k.sh --codec hevc_nvenc      (any render_video.py option)
#   tools/render_4k.sh --bench                 (time a frame per chapter and estimate the render first)
set -eu
cd "$(dirname "$0")/.."

for tool in cmake ffmpeg ffprobe python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "render_4k: $tool not found on PATH" >&2; exit 1; }
done
[ -f assets/music/show.wav ] || { echo "render_4k: assets/music/show.wav is missing (tools/prepare_show.py)" >&2; exit 1; }

cmake --preset linux
cmake --build --preset linux --target Evolution

mkdir -p dist/render
exec python3 tools/render_video.py --exe build/linux/bin/Evolution --music assets/music/show.wav \
    --out dist/render/EVOLUTION-2160p60.mp4 --width 3840 --height 2160 --fps 60 --spp 64 "$@"
