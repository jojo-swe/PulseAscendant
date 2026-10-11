param([string]$BuildDirectory="$PSScriptRoot/../build/windows")
$ErrorActionPreference='Stop'
$remixRoot=Split-Path $PSScriptRoot -Parent
$stage=Join-Path $remixRoot ("build/stage/windows-"+[DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
$dist=Join-Path $remixRoot 'dist'
New-Item -ItemType Directory $dist -Force | Out-Null
& cmake --install $BuildDirectory --config Release --prefix $stage
if($LASTEXITCODE -ne 0){throw 'Install failed'}
$exe=Join-Path $stage 'SecondRealityRemix.exe'
if(!(Test-Path $exe) -or !(Test-Path "$stage/assets/music/luminous-flow.wav")){throw 'Incomplete package'}
# Launch from an unrelated working directory to exercise executable-relative resources.
Push-Location ([IO.Path]::GetTempPath())
try {& $exe --time 42 --duration 1 --hidden; if($LASTEXITCODE -ne 0){throw 'Staged launch failed'}}finally{Pop-Location}
$zip=Join-Path $dist 'SecondRealityRemix-Windows-x64.zip'
Compress-Archive -Path "$stage/*" -DestinationPath $zip -Force
Get-FileHash -LiteralPath $zip
Write-Output "STAGE: $stage"
Write-Output "PACKAGE: $zip"
