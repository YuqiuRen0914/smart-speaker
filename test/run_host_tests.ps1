param([string]$Cxx = 'c++')
$ErrorActionPreference = 'Stop'
$appRoot = Split-Path $PSScriptRoot -Parent
$repoRoot = [IO.Path]::GetFullPath((Join-Path $appRoot '../../..'))
$testRoot = Join-Path $repoRoot '.build/products/smart-speaker/host-tests'
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null
$include = Join-Path $appRoot 'main/smart_speaker'
foreach ($name in @('dsp', 'i2s_sync', 'boot_confirmation', 'character', 'frame_delta', 'track_caption', 'lyrics', 'pause_indicator')) {
    $sources = @((Join-Path $PSScriptRoot ($name + '_test.cpp')))
    if ($name -eq 'dsp') { $sources += Join-Path $include 'smart_speaker_dsp.cpp' }
    if ($name -in @('character', 'frame_delta')) { $sources += Join-Path $include 'smart_speaker_character.cpp' }
    $binary = Join-Path $testRoot ($name + '_test.exe')
    & $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$include" "-I$repoRoot/lunaros/include" "-I$repoRoot/components/lunaredu_character_assets/include" @sources -o $binary
    if ($LASTEXITCODE -ne 0) { throw "$name test compilation failed" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "$name test failed" }
}

foreach ($name in @('audio_lifecycle', 'transfer', 'lyrics_api', 'track_remove', 'local_menu', 'playback_mode', 'mp3_guard')) {
    & python (Join-Path $PSScriptRoot ($name + '_test.py'))
    if ($LASTEXITCODE -ne 0) { throw "$name production regression failed" }
}
