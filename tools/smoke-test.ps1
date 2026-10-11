param([string]$Exe="$PSScriptRoot/../build/windows/Release/SecondRealityRemix.exe",[string]$Output="$PSScriptRoot/../build/captures")
$ErrorActionPreference='Stop'
if (!(Test-Path -LiteralPath $Exe)) { throw 'Renderer executable missing' }
& $Exe --smoke-test --no-audio --capture-dir $Output
if ($LASTEXITCODE -ne 0) { throw 'Renderer smoke test failed' }
foreach($scene in 1..8) { foreach($mode in 'modern','retro') {
 $path=Join-Path $Output "scene-$scene-$mode.bmp"
 if (!(Test-Path $path) -or (Get-Item $path).Length -lt 100000) { throw "Missing rendered image: $path" }
}}
Write-Output 'All sixteen renderer captures present'
