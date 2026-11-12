param([string]$Cxx = 'c++')
$ErrorActionPreference = 'Stop'
$appRoot = Split-Path $PSScriptRoot -Parent
$repoRoot = $appRoot
$testRoot = Join-Path $repoRoot '.build/products/smart-speaker/host-tests'
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null
$include = Join-Path $appRoot 'main/smart_speaker'
$productInclude = Join-Path $appRoot 'main/product'
$assistantMcpBinary = Join-Path $testRoot 'assistant_mcp_dispatch_test.exe'
$assistantMcpInclude = Join-Path $repoRoot 'lunaros_apps/lunaredu_pet/src'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$assistantMcpInclude" "-I$repoRoot/lunaros_apps/lunaredu_pet/include" "-I$repoRoot/lunaros_apps/lunaredu_pet/test/host/mcp" "-I$repoRoot/managed_components/bblanchon__arduinojson/src" "-I$repoRoot/lunaros/include" (Join-Path $repoRoot 'lunaros_apps/lunaredu_pet/test/interaction/assistant_mcp_dispatch_test.cpp') (Join-Path $assistantMcpInclude 'interaction/tools/assistant_mcp.cpp') -o $assistantMcpBinary
if ($LASTEXITCODE -ne 0) { throw 'assistant MCP dispatch test compilation failed' }
& $assistantMcpBinary
if ($LASTEXITCODE -ne 0) { throw 'assistant MCP dispatch test failed' }
$playbackToolStatusBinary = Join-Path $testRoot 'playback_tool_status_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$include" "-I$repoRoot/managed_components/bblanchon__arduinojson/src" (Join-Path $PSScriptRoot 'playback_tool_status_test.cpp') -o $playbackToolStatusBinary
if ($LASTEXITCODE -ne 0) { throw 'playback tool status test compilation failed' }
& $playbackToolStatusBinary
if ($LASTEXITCODE -ne 0) { throw 'playback tool status test failed' }
$musicSpectrumBinary = Join-Path $testRoot 'music_spectrum_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 -pthread "-I$include" (Join-Path $PSScriptRoot 'music_spectrum_test.cpp') -o $musicSpectrumBinary
if ($LASTEXITCODE -ne 0) { throw 'music spectrum test compilation failed' }
& $musicSpectrumBinary
if ($LASTEXITCODE -ne 0) { throw 'music spectrum test failed' }
$noiseBinary = Join-Path $testRoot 'noise_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$include" (Join-Path $PSScriptRoot 'noise_test.cpp') (Join-Path $include 'smart_speaker_noise.cpp') -o $noiseBinary
if ($LASTEXITCODE -ne 0) { throw 'noise test compilation failed' }
& $noiseBinary
if ($LASTEXITCODE -ne 0) { throw 'noise test failed' }
$productBinary = Join-Path $testRoot 'product_state_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$productInclude" (Join-Path $PSScriptRoot 'product_state_test.cpp') (Join-Path $productInclude 'product_state.cpp') -o $productBinary
if ($LASTEXITCODE -ne 0) { throw 'product state test compilation failed' }
& $productBinary
if ($LASTEXITCODE -ne 0) { throw 'product state test failed' }
$assistantTurnBinary = Join-Path $testRoot 'assistant_turn_policy_test.exe'
$assistantTurnInclude = Join-Path $repoRoot 'lunaros_apps/lunaredu_pet/src'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$assistantTurnInclude" (Join-Path $repoRoot 'lunaros_apps/lunaredu_pet/test/interaction/assistant_turn_policy_test.cpp') -o $assistantTurnBinary
if ($LASTEXITCODE -ne 0) { throw 'assistant turn policy test compilation failed' }
& $assistantTurnBinary
if ($LASTEXITCODE -ne 0) { throw 'assistant turn policy test failed' }
$assistantEndpointBinary = Join-Path $testRoot 'assistant_endpoint_policy_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$assistantTurnInclude" (Join-Path $repoRoot 'lunaros_apps/lunaredu_pet/test/interaction/assistant_endpoint_policy_test.cpp') -o $assistantEndpointBinary
if ($LASTEXITCODE -ne 0) { throw 'assistant endpoint policy test compilation failed' }
& $assistantEndpointBinary
if ($LASTEXITCODE -ne 0) { throw 'assistant endpoint policy test failed' }
$focusPolicyBinary = Join-Path $testRoot 'smart_speaker_focus_policy_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$include" (Join-Path $PSScriptRoot 'smart_speaker_focus_policy_test.cpp') -o $focusPolicyBinary
if ($LASTEXITCODE -ne 0) { throw 'speaker focus policy test compilation failed' }
& $focusPolicyBinary
if ($LASTEXITCODE -ne 0) { throw 'speaker focus policy test failed' }
$weatherBinary = Join-Path $testRoot 'weather_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$productInclude" "-I$repoRoot/lunaros/include" "-I$repoRoot/managed_components/bblanchon__arduinojson/src" "-I$PSScriptRoot" (Join-Path $PSScriptRoot 'weather_test.cpp') (Join-Path $PSScriptRoot 'host/product_time_storage.cpp') (Join-Path $productInclude 'weather.cpp') -o $weatherBinary
if ($LASTEXITCODE -ne 0) { throw 'weather test compilation failed' }
& $weatherBinary
if ($LASTEXITCODE -ne 0) { throw 'weather test failed' }
$productTimeBinary = Join-Path $testRoot 'product_time_persistence_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$appRoot/main" "-I$productInclude" "-I$repoRoot/lunaros/include" "-I$repoRoot/managed_components/bblanchon__arduinojson/src" "-I$PSScriptRoot/host" (Join-Path $PSScriptRoot 'product_time_persistence_test.cpp') (Join-Path $PSScriptRoot 'host/product_time_storage.cpp') (Join-Path $productInclude 'product_state.cpp') (Join-Path $productInclude 'product_state_runtime.cpp') -o $productTimeBinary
if ($LASTEXITCODE -ne 0) { throw 'product time persistence test compilation failed' }
& $productTimeBinary
if ($LASTEXITCODE -ne 0) { throw 'product time persistence test failed' }
$desktopBinary = Join-Path $testRoot 'desktop_resources_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$productInclude" "-I$repoRoot/lunaros/include" "-I$repoRoot/managed_components/bblanchon__arduinojson/src" "-I$repoRoot/components_device/media/lunaredu_device_sdcard/include" (Join-Path $PSScriptRoot 'desktop_resources_test.cpp') (Join-Path $productInclude 'desktop_resources.cpp') (Join-Path $productInclude 'product_state.cpp') -o $desktopBinary
if ($LASTEXITCODE -ne 0) { throw 'desktop resources test compilation failed' }
& $desktopBinary
if ($LASTEXITCODE -ne 0) { throw 'desktop resources test failed' }
$petAnimationBinary = Join-Path $testRoot 'pet_animation_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$appRoot/main" "-I$include" (Join-Path $PSScriptRoot 'pet_animation_test.cpp') -o $petAnimationBinary
if ($LASTEXITCODE -ne 0) { throw 'pet animation test compilation failed' }
& $petAnimationBinary
if ($LASTEXITCODE -ne 0) { throw 'pet animation test failed' }
$touchBinary = Join-Path $testRoot 'touch_routing_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$include" (Join-Path $PSScriptRoot 'touch_routing_test.cpp') (Join-Path $include 'smart_speaker_touch_routing.cpp') -o $touchBinary
if ($LASTEXITCODE -ne 0) { throw 'touch routing test compilation failed' }
& $touchBinary
if ($LASTEXITCODE -ne 0) { throw 'touch routing test failed' }
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
$petFramePolicyBinary = Join-Path $testRoot 'pet_frame_policy_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$include" "-I$repoRoot/main/product" (Join-Path $PSScriptRoot 'pet_frame_policy_test.cpp') -o $petFramePolicyBinary
if ($LASTEXITCODE -ne 0) { throw 'pet frame policy test compilation failed' }
& $petFramePolicyBinary
if ($LASTEXITCODE -ne 0) { throw 'pet frame policy test failed' }
$coverBinary = Join-Path $testRoot 'cover_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$include" (Join-Path $PSScriptRoot 'cover_test.cpp') (Join-Path $include 'smart_speaker_cover.cpp') -o $coverBinary
if ($LASTEXITCODE -ne 0) { throw 'cover test compilation failed' }
& $coverBinary
if ($LASTEXITCODE -ne 0) { throw 'cover test failed' }

foreach ($name in @('xiaozhi_activation', 'music_voice_queue', 'lunarlink_protocol', 'lunarlink_bootstrap', 'lunarlink_binding', 'lunarlink_generation', 'lunarlink_capabilities', 'audio_lifecycle', 'transfer', 'lyrics_api', 'cover_api', 'pet_frame_api', 'track_remove', 'local_menu_display', 'local_menu', 'playback_mode', 'mp3_guard', 'sd_dma', 'lcd_strip', 'audio_drain', 'music_damage', 'audio_read_guard', 'audio_focus_state', 'assistant_control', 'wake_model', 'microphone_capture')) {
    & python (Join-Path $PSScriptRoot ($name + '_test.py'))
    if ($LASTEXITCODE -ne 0) { throw "$name production regression failed" }
}

$woodFishBinary = Join-Path $testRoot 'wood_fish_sound_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 "-I$include" (Join-Path $PSScriptRoot 'wood_fish_sound_test.cpp') (Join-Path $include 'smart_speaker_dsp.cpp') -o $woodFishBinary
if ($LASTEXITCODE -ne 0) { throw 'wood fish sound test compilation failed' }
& $woodFishBinary
if ($LASTEXITCODE -ne 0) { throw 'wood fish sound test failed' }

node "$appRoot/test/music_metadata_test.mjs"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

node "$appRoot/test/library_refresh_test.mjs"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
