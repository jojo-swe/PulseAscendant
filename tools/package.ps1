param([string]$BuildDirectory="$PSScriptRoot/../build/windows",[string]$OutputDirectory="$PSScriptRoot/../dist/three-acts")
$ErrorActionPreference='Stop'
$remixRoot=Split-Path $PSScriptRoot -Parent
$stage=Join-Path $remixRoot ("build/stage/windows-"+[DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
$dist=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory $dist -Force | Out-Null
& cmake --install $BuildDirectory --config Release --prefix $stage
if($LASTEXITCODE -ne 0){throw 'Install failed'}
$exe=Join-Path $stage 'PulseAscendant.exe'
if(!(Test-Path $exe) -or !(Test-Path "$stage/assets/music/second-reality-three-acts.wav")){throw 'Incomplete package'}
# Launch from an unrelated working directory to exercise executable-relative resources.
Push-Location ([IO.Path]::GetTempPath())
try {foreach($showTime in 42,342,620){& $exe --time $showTime --duration 1 --hidden --width 640 --height 360; if($LASTEXITCODE -ne 0){throw "Staged launch failed at $showTime"}}}finally{Pop-Location}
$zip=Join-Path $dist 'PulseAscendant-Windows-x64.zip'
Compress-Archive -Path "$stage/*" -DestinationPath $zip -Force
Get-FileHash -LiteralPath $zip
Write-Output "STAGE: $stage"
Write-Output "PACKAGE: $zip"
