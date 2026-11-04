#!/bin/sh
# Renders EVOLUTION at 3840x2160, 60 fps and --spp samples per pixel into dist/render/EVOLUTION-2160p60.mp4 (H.264
# High, BT.709, AAC 384 kb/s): configures and builds the Release preset, checks for ffmpeg, then runs
# tools/render_video.py, which renders in 10-second chunks. Interrupted? Run the same command again: finished
# chunks are kept and the render carries on from the first missing one.
#
#   tools/render_4k.sh
#   tools/render_4k.sh --codec hevc_nvenc      (any render_video.py option)
#   tools/render_4k.sh --bench                 (time a frame per chapter and estimate the render first)
#   tools/render_4k.sh --spp 64                (--spp 16 (default, about 0.25 of the 64-spp time); --spp 64 for
#                                              maximum quality; --bench times frames at the same --spp)
set -eu
cd "$(dirname "$0")/.."

# --spp N (default 16; exports from 16 spp run the a-trous filter); every other argument goes to render_video.py.
spp=16
n=$#
while [ "$n" -gt 0 ]; do
    arg=$1; shift; n=$((n - 1))
    if [ "$arg" = "--spp" ]; then
        [ "$n" -gt 0 ] || { echo "render_4k: --spp needs a number" >&2; exit 1; }
        spp=$1; shift; n=$((n - 1))
    else
        set -- "$@" "$arg"
    fi
done

for tool in cmake ffmpeg ffprobe python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "render_4k: $tool not found on PATH" >&2; exit 1; }
done
[ -f assets/music/show.wav ] || { echo "render_4k: assets/music/show.wav is missing (tools/prepare_show.py)" >&2; exit 1; }

cmake --preset linux
cmake --build --preset linux --target Evolution

mkdir -p dist/render
exec python3 tools/render_video.py --exe build/linux/bin/Evolution --music assets/music/show.wav \
    --out dist/render/EVOLUTION-2160p60.mp4 --width 3840 --height 2160 --fps 60 --spp "$spp" "$@"
