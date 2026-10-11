param([string]$SourcePath='')
$ErrorActionPreference = 'Stop'
$remixRoot = Split-Path $PSScriptRoot -Parent
$source = if($SourcePath){$SourcePath}else{"$remixRoot/music/source/Luminous Flow.mp3"}
$expected = 'FD8C915C3D7A52CB3175B3CE84BCEADF99405E6EA2EFA675651838161A5ADFF3'
if ((Get-FileHash -LiteralPath $source).Hash -ne $expected) { throw 'Source music digest mismatch' }
New-Item -ItemType Directory -Path "$remixRoot/music/source","$remixRoot/assets/music" -Force | Out-Null
$destination="$remixRoot/music/source/Luminous Flow.mp3"
if([IO.Path]::GetFullPath($source) -ne [IO.Path]::GetFullPath($destination)){Copy-Item -LiteralPath $source -Destination $destination}
& ffmpeg -hide_banner -loglevel error -y -i $source -map 0:a:0 -ar 48000 -ac 2 -c:a pcm_s16le "$remixRoot/assets/music/luminous-flow.wav"
if ($LASTEXITCODE -ne 0) { throw 'Music preparation failed' }
& python "$PSScriptRoot/analyze_music.py" "$remixRoot/assets/music/luminous-flow.wav" "$remixRoot/src/music_data.hpp" "$remixRoot/music/analysis.json"
if ($LASTEXITCODE -ne 0) { throw 'Music analysis failed (requires Python + numpy)' }
$analysis=Get-Content "$remixRoot/music/analysis.json" -Raw | ConvertFrom-Json
$receipt = [ordered]@{ title='Luminous Flow'; source_sha256=$expected.ToLower(); source_file='music/source/Luminous Flow.mp3'; source_duration=472.36; suno_id='1f52e6e8-5a07-45da-9c9b-09753a609eb3'; excerpt_start=0; excerpt_duration=$analysis.duration; runtime_frames=$analysis.frames; sample_rate=48000; channels=2; encoding='PCM signed 16 bit'; fades='none; complete decoded source'; runtime_sha256=(Get-FileHash "$remixRoot/assets/music/luminous-flow.wav").Hash.ToLower(); listening_acceptance='pending'; distribution_rights='unverified' }
$receipt | ConvertTo-Json | Set-Content -Encoding utf8 "$remixRoot/music/asset-receipt.json"
