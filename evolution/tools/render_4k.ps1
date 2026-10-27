# Renders EVOLUTION at 3840x2160 (or -Height 1440 / 1080), 60 fps and -Spp samples per pixel into
# dist\render\EVOLUTION-<height>p60.mp4 (H.264 High, BT.709, AAC 384 kb/s): configures and builds the Release
# preset, checks for ffmpeg, then runs
# tools\render_video.py, which renders in 10-second chunks. Interrupted? Run the same command again: finished
# chunks are kept and the render carries on from the first missing one.
#
#   powershell -ExecutionPolicy Bypass -File tools\render_4k.ps1
#   tools\render_4k.ps1 -Codec hevc_nvenc        (NVENC instead of x264)
#   tools\render_4k.ps1 -Bench                   (time a frame per chapter and estimate the render first)
#   tools\render_4k.ps1 -Spp 64                  (-Spp 16 (default, about 0.25 of the 64-spp time); -Spp 64 for
#                                                maximum quality; -Bench times frames at the same -Spp)
#   tools\render_4k.ps1 -Height 1080             (1920x1080; -Height 1440: 2560x1440; default 2160: 3840x2160)
#   tools\render_4k.ps1 -ShutterSlices 4         (the 180 degree shutter in 4 time slices whatever -Spp: motion
#                                                blur at -Spp 4, which otherwise has none; other bytes)
#   tools\render_4k.ps1 -Preset windows-vs2022   (pick the CMake preset; by default the newest Visual Studio)
#
# Presets: "windows" (Visual Studio 2026, CMake 4.2 or newer), "windows-vs2022" (Visual Studio 2022) and
# "windows-ninja" (Ninja and cl.exe, from a Developer PowerShell); each builds into build\<preset>.
param(
    [ValidateSet("x264", "hevc_nvenc", "ffv1")] [string] $Codec = "x264",
    [ValidateSet("auto", "windows", "windows-vs2022", "windows-ninja")] [string] $Preset = "auto",
    [int] $Spp = 16,
    [ValidateSet(2160, 1440, 1080)] [int] $Height = 2160,
    [ValidateRange(0, 8)] [int] $ShutterSlices = 0,
    [switch] $Coda,
    [switch] $Bench
)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

foreach ($tool in @("cmake", "ffmpeg", "ffprobe")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "render_4k: $tool not found on PATH" }
}

# @( ) keeps the command an array even when it has one element (PowerShell unrolls a one-element array that
# comes out of an if), so $python[0] is the command and not its first character.
[string[]] $python = @(if (Get-Command py -ErrorAction SilentlyContinue) { "py", "-3" } else { "python" })
$pythonExe = $python[0]
[string[]] $pythonArgs = @($python | Select-Object -Skip 1)
# The Store's "python" alias only opens the Store: make sure this is a Python 3 that runs.
& $pythonExe @pythonArgs -c "import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)"
if ($LASTEXITCODE -ne 0) { throw "render_4k: Python 3 not found (install it from python.org or with winget)" }
if (-not (Test-Path "assets\music\show.wav")) { throw "render_4k: assets\music\show.wav is missing (tools\prepare_show.py)" }

if ($Preset -eq "auto") {
    # The newest Visual Studio with the C++ tools, as vswhere reports it; its 2026 generator also needs
    # CMake 4.2. Without either, a Developer PowerShell's cl.exe with Ninja.
    $majors = @()
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $majors = @(& $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion |
                    ForEach-Object { [int]($_.Split(".")[0]) })
    }
    $cmakeLine = @(cmake --version)[0]
    $cmakeVersion = [version]($cmakeLine -replace '^cmake version\s+', '' -replace '[^0-9.].*$', '')
    if (($majors -contains 18) -and ($cmakeVersion -ge [version]"4.2")) { $Preset = "windows" }
    elseif ($majors -contains 17) { $Preset = "windows-vs2022" }
    elseif ((Get-Command cl -ErrorAction SilentlyContinue) -and (Get-Command ninja -ErrorAction SilentlyContinue)) {
        $Preset = "windows-ninja"
    }
    else { throw "render_4k: no Visual Studio 2022 or 2026 with the C++ tools found (or pass -Preset)" }
    Write-Host "render_4k: preset $Preset"
}

cmake --preset $Preset
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build --preset $Preset --target Evolution
if ($LASTEXITCODE) { exit $LASTEXITCODE }

New-Item -ItemType Directory -Force "dist\render" | Out-Null
$width = @{ 2160 = 3840; 1440 = 2560; 1080 = 1920 }[$Height]
$name = "EVOLUTION-$($Height)p60"
if ($ShutterSlices -gt 0) { $name += "-s$ShutterSlices" }
$out = if ($Codec -eq "ffv1") { "dist\render\$name.mkv" } else { "dist\render\$name.mp4" }
[string[]] $arguments = @("tools\render_video.py", "--exe", "build\$Preset\bin\Evolution.exe",
                          "--music", "assets\music\show.wav", "--out", $out, "--width", "$width",
                          "--height", "$Height", "--fps", "60", "--spp", "$Spp", "--codec", $Codec)
if ($Coda) { $arguments += "--coda" }
if ($Bench) { $arguments += "--bench" }
# Options for Evolution itself go after "--" (render_video.py passes them on and records them with the parts).
if ($ShutterSlices -gt 0) { $arguments += @("--", "--shutter-slices", "$ShutterSlices") }
& $pythonExe @pythonArgs @arguments
exit $LASTEXITCODE
