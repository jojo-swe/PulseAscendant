$ErrorActionPreference='Stop'
$remixRoot=Split-Path $PSScriptRoot -Parent
$zip=Join-Path $remixRoot 'dist/SecondRealityRemix-source.zip'
New-Item -ItemType Directory "$remixRoot/dist" -Force | Out-Null
Push-Location $remixRoot
try {& git archive --format=zip --output=$zip HEAD; if($LASTEXITCODE -ne 0){throw 'Source archive failed'}}finally{Pop-Location}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::Open($zip,[IO.Compression.ZipArchiveMode]::Update)
try {
 foreach($relative in 'assets/music/luminous-flow.wav','music/source/Luminous Flow.mp3'){
  [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,(Join-Path $remixRoot $relative),$relative,[IO.Compression.CompressionLevel]::Optimal) | Out-Null
 }
}finally{$archive.Dispose()}
Get-FileHash -LiteralPath $zip
