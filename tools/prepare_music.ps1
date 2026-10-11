param(
 [Alias('SourcePath')][string]$Act1Path='',
 [string]$Act2Path='',
 [string]$Act3Path=''
)
$ErrorActionPreference='Stop'
$remixRoot=Split-Path $PSScriptRoot -Parent
if(!$Act1Path){$Act1Path=Join-Path $remixRoot 'music/source/Luminous Flow.mp3'}
if(!$Act2Path){$Act2Path=Join-Path $remixRoot 'music/source/Adventurous Uplift.mp3'}
if(!$Act3Path){$Act3Path=Join-Path $remixRoot 'music/source/Rigid Arpeggios.mp3'}
& python "$PSScriptRoot/prepare_show.py" $Act1Path $Act2Path $Act3Path
if($LASTEXITCODE -ne 0){throw 'Three-act audio preparation failed (requires ffmpeg and Python + numpy)'}
