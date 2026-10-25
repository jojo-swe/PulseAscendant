# Renders EVOLUTION at 3840x2160, 60 fps and 64 samples per pixel into dist\render\EVOLUTION-2160p60.mp4 (H.264
# High, BT.709, AAC 384 kb/s): configures and builds the Release preset, checks for ffmpeg, then runs
# tools\render_video.py, which renders in 10-second chunks. Interrupted? Run the same command again: finished
# chunks are kept and the render carries on from the first missing one.
#
#   powershell -ExecutionPolicy Bypass -File tools\render_4k.ps1
#   tools\render_4k.ps1 -Codec hevc_nvenc        (NVENC instead of x264)
#   tools\render_4k.ps1 -Bench                   (time a frame per chapter and estimate the render first)
param(
    [ValidateSet("x264", "hevc_nvenc", "ffv1")] [string] $Codec = "x264",
    [switch] $Coda,
    [switch] $Bench
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

foreach ($tool in "cmake", "ffmpeg", "ffprobe") {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "render_4k: $tool not found on PATH" }
}
$python = if (Get-Command py -ErrorAction SilentlyContinue) { @("py", "-3") } else { @("python") }
if (-not (Test-Path "assets\music\show.wav")) { throw "render_4k: assets\music\show.wav is missing (tools\prepare_show.py)" }

cmake --preset windows
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build --preset windows --target Evolution
if ($LASTEXITCODE) { exit $LASTEXITCODE }

New-Item -ItemType Directory -Force "dist\render" | Out-Null
$out = if ($Codec -eq "ffv1") { "dist\render\EVOLUTION-2160p60.mkv" } else { "dist\render\EVOLUTION-2160p60.mp4" }
$arguments = @("tools\render_video.py", "--exe", "build\windows\bin\Evolution.exe", "--music", "assets\music\show.wav",
               "--out", $out, "--width", "3840", "--height", "2160", "--fps", "60", "--spp", "64", "--codec", $Codec)
if ($Coda) { $arguments += "--coda" }
if ($Bench) { $arguments += "--bench" }
$exe = $python[0]
$rest = @($python | Select-Object -Skip 1) + $arguments
& $exe @rest
exit $LASTEXITCODE
