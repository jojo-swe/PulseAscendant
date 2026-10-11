param([string]$OutputDirectory="$PSScriptRoot/../dist/three-acts")
$ErrorActionPreference='Stop'
$remixRoot=Split-Path $PSScriptRoot -Parent
$zip=Join-Path $OutputDirectory 'PulseAscendant-source.zip'
New-Item -ItemType Directory $OutputDirectory -Force | Out-Null
Push-Location $remixRoot
try {& git archive --format=zip --output=$zip HEAD; if($LASTEXITCODE -ne 0){throw 'Source archive failed'}}finally{Pop-Location}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::Open($zip,[IO.Compression.ZipArchiveMode]::Update)
try {
 foreach($relative in 'assets/music/luminous-flow-act1.wav','assets/music/adventurous-uplift.wav','assets/music/rigid-arpeggios.wav','assets/music/second-reality-three-acts.wav','music/source/Luminous Flow.mp3','music/source/Adventurous Uplift.mp3','music/source/Rigid Arpeggios.mp3'){
  [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,(Join-Path $remixRoot $relative),$relative,[IO.Compression.CompressionLevel]::Optimal) | Out-Null
 }
}finally{$archive.Dispose()}
Get-FileHash -LiteralPath $zip
