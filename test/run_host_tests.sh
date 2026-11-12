#!/bin/sh
set -eu
APP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CXX=${CXX:-c++}
MUSIC_SPECTRUM_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-music-spectrum-test
$CXX -std=c++17 -Wall -Wextra -Werror -O2 -pthread -I"$APP_DIR/main/smart_speaker" \
  "$APP_DIR/test/music_spectrum_test.cpp" -o "$MUSIC_SPECTRUM_TEST_BIN"
"$MUSIC_SPECTRUM_TEST_BIN"
ASSISTANT_MCP_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-assistant-mcp-dispatch-test
$CXX -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/lunaros_apps/lunaredu_pet/src" \
  -I"$APP_DIR/lunaros_apps/lunaredu_pet/include" \
  -I"$APP_DIR/lunaros_apps/lunaredu_pet/test/host/mcp" \
  -I"$APP_DIR/managed_components/bblanchon__arduinojson/src" \
  -I"$APP_DIR/lunaros/include" \
  "$APP_DIR/lunaros_apps/lunaredu_pet/test/interaction/assistant_mcp_dispatch_test.cpp" \
  "$APP_DIR/lunaros_apps/lunaredu_pet/src/interaction/tools/assistant_mcp.cpp" \
  -o "$ASSISTANT_MCP_TEST_BIN"
"$ASSISTANT_MCP_TEST_BIN"
PLAYBACK_TOOL_STATUS_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-playback-tool-status-test
$CXX -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/main/smart_speaker" \
  -I"$APP_DIR/managed_components/bblanchon__arduinojson/src" \
  "$APP_DIR/test/playback_tool_status_test.cpp" -o "$PLAYBACK_TOOL_STATUS_TEST_BIN"
"$PLAYBACK_TOOL_STATUS_TEST_BIN"
NOISE_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-noise-test
$CXX -std=c++17 -Wall -Wextra -Werror -O2 -I"$APP_DIR/main/smart_speaker" \
  "$APP_DIR/test/noise_test.cpp" "$APP_DIR/main/smart_speaker/smart_speaker_noise.cpp" -o "$NOISE_TEST_BIN"
"$NOISE_TEST_BIN"
PRODUCT_STATE_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-product-state-test
$CXX -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/main/product" \
  "$APP_DIR/test/product_state_test.cpp" "$APP_DIR/main/product/product_state.cpp" \
  -o "$PRODUCT_STATE_TEST_BIN"
"$PRODUCT_STATE_TEST_BIN"
ASSISTANT_TURN_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-assistant-turn-policy-test
$CXX -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/lunaros_apps/lunaredu_pet/src" \
  "$APP_DIR/lunaros_apps/lunaredu_pet/test/interaction/assistant_turn_policy_test.cpp" \
  -o "$ASSISTANT_TURN_TEST_BIN"
"$ASSISTANT_TURN_TEST_BIN"
ASSISTANT_ENDPOINT_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-assistant-endpoint-policy-test
$CXX -std=c++17 -Wall -Wextra -Werror -O2 -I"$APP_DIR/lunaros_apps/lunaredu_pet/src" \
  "$APP_DIR/lunaros_apps/lunaredu_pet/test/interaction/assistant_endpoint_policy_test.cpp" \
  -o "$ASSISTANT_ENDPOINT_TEST_BIN"
"$ASSISTANT_ENDPOINT_TEST_BIN"
FOCUS_POLICY_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-focus-policy-test
$CXX -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/main/smart_speaker" \
  "$APP_DIR/test/smart_speaker_focus_policy_test.cpp" \
  -o "$FOCUS_POLICY_TEST_BIN"
"$FOCUS_POLICY_TEST_BIN"
WEATHER_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-weather-test
$CXX -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/main/product" -I"$APP_DIR/lunaros/include" \
  -I"$APP_DIR/managed_components/bblanchon__arduinojson/src" -I"$APP_DIR/test" \
  "$APP_DIR/test/weather_test.cpp" "$APP_DIR/test/host/product_time_storage.cpp" \
  "$APP_DIR/main/product/weather.cpp" -o "$WEATHER_TEST_BIN"
"$WEATHER_TEST_BIN"
PRODUCT_TIME_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-product-time-test
$CXX -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/main" -I"$APP_DIR/main/product" -I"$APP_DIR/lunaros/include" \
  -I"$APP_DIR/managed_components/bblanchon__arduinojson/src" -I"$APP_DIR/test/host" \
  "$APP_DIR/test/product_time_persistence_test.cpp" "$APP_DIR/test/host/product_time_storage.cpp" \
  "$APP_DIR/main/product/product_state.cpp" "$APP_DIR/main/product/product_state_runtime.cpp" \
  -o "$PRODUCT_TIME_TEST_BIN"
"$PRODUCT_TIME_TEST_BIN"
DESKTOP_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-desktop-test
$CXX -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/main/product" -I"$APP_DIR/lunaros/include" \
  -I"$APP_DIR/managed_components/bblanchon__arduinojson/src" \
  -I"$APP_DIR/components_device/media/lunaredu_device_sdcard/include" \
  "$APP_DIR/test/desktop_resources_test.cpp" "$APP_DIR/main/product/desktop_resources.cpp" \
  "$APP_DIR/main/product/product_state.cpp" -o "$DESKTOP_TEST_BIN"
"$DESKTOP_TEST_BIN"
TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-dsp-test
SYNC_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-i2s-sync-test
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/main/smart_speaker" -I"$APP_DIR/components/lunaredu_character_assets/include" \
  "$APP_DIR/test/dsp_test.cpp" \
  "$APP_DIR/main/smart_speaker/smart_speaker_dsp.cpp" \
  -o "$TEST_BIN"
"$TEST_BIN"
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/main/smart_speaker" -I"$APP_DIR/components/lunaredu_character_assets/include" \
  "$APP_DIR/test/i2s_sync_test.cpp" \
  -o "$SYNC_TEST_BIN"
"$SYNC_TEST_BIN"
TOUCH_ROUTING_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-touch-routing-test
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/main/smart_speaker" \
  "$APP_DIR/test/touch_routing_test.cpp" \
  "$APP_DIR/main/smart_speaker/smart_speaker_touch_routing.cpp" \
  -o "$TOUCH_ROUTING_TEST_BIN"
"$TOUCH_ROUTING_TEST_BIN"
PET_ANIMATION_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-pet-animation-test
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/main" -I"$APP_DIR/main/smart_speaker" \
  "$APP_DIR/test/pet_animation_test.cpp" -o "$PET_ANIMATION_TEST_BIN"
"$PET_ANIMATION_TEST_BIN"
CHARACTER_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-character-test
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/main/smart_speaker" -I"$APP_DIR/components/lunaredu_character_assets/include" \
  "$APP_DIR/test/character_test.cpp" \
  "$APP_DIR/main/smart_speaker/smart_speaker_character.cpp" \
  -o "$CHARACTER_TEST_BIN"
"$CHARACTER_TEST_BIN"
PET_FRAME_POLICY_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-pet-frame-policy-test
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/main/smart_speaker" -I"$APP_DIR/main/product" \
  "$APP_DIR/test/pet_frame_policy_test.cpp" -o "$PET_FRAME_POLICY_TEST_BIN"
"$PET_FRAME_POLICY_TEST_BIN"
DELTA_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-frame-delta-test
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/main/smart_speaker" -I"$APP_DIR/components/lunaredu_character_assets/include" \
  "$APP_DIR/test/frame_delta_test.cpp" \
  "$APP_DIR/main/smart_speaker/smart_speaker_character.cpp" \
  -o "$DELTA_TEST_BIN"
"$DELTA_TEST_BIN"
CAPTION_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-caption-test
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/main/smart_speaker" -I"$APP_DIR/components/lunaredu_character_assets/include" \
  "$APP_DIR/test/track_caption_test.cpp" \
  -o "$CAPTION_TEST_BIN"
"$CAPTION_TEST_BIN"

LYRICS_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-lyrics-test
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I"$APP_DIR/main/smart_speaker" -I"$APP_DIR/components/lunaredu_character_assets/include" "$APP_DIR/test/lyrics_test.cpp" -o "$LYRICS_TEST_BIN"
"$LYRICS_TEST_BIN"

python3 "$APP_DIR/test/sd_recovery_test.py"
PAUSE_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-pause-indicator-test
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I"$APP_DIR/main/smart_speaker" -I"$APP_DIR/components/lunaredu_character_assets/include" "$APP_DIR/test/pause_indicator_test.cpp" -o "$PAUSE_TEST_BIN"
"$PAUSE_TEST_BIN"

COVER_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-cover-test
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I"$APP_DIR/main/smart_speaker" \
  "$APP_DIR/test/cover_test.cpp" "$APP_DIR/main/smart_speaker/smart_speaker_cover.cpp" \
  -o "$COVER_TEST_BIN"
"$COVER_TEST_BIN"

BOOT_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-boot-confirmation-test
"$CXX" -std=c++20 -Wall -Wextra -Werror -I"$APP_DIR/lunaros/include" "$APP_DIR/test/boot_confirmation_test.cpp" -o "$BOOT_TEST_BIN"
"$BOOT_TEST_BIN"
for name in xiaozhi_activation music_voice_queue lunarlink_protocol lunarlink_bootstrap lunarlink_binding lunarlink_generation lunarlink_capabilities audio_lifecycle transfer lyrics_api cover_api pet_frame_api track_remove sd_dma lcd_strip audio_drain music_damage local_menu_display local_menu playback_mode mp3_guard audio_read_guard; do
    python3 "$APP_DIR/test/${name}_test.py"
done

python3 "$APP_DIR/test/avatar_test.py"
python3 "$APP_DIR/test/input_budget_test.py"
python3 "$APP_DIR/test/audio_focus_state_test.py"
python3 "$APP_DIR/test/assistant_control_test.py"
python3 "$APP_DIR/test/wake_model_test.py"
python3 "$APP_DIR/test/microphone_capture_test.py"
node "$APP_DIR/test/web_api_test.mjs"

WOOD_FISH_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-wood-fish-sound-test
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I"$APP_DIR/main/smart_speaker" \
  "$APP_DIR/test/wood_fish_sound_test.cpp" "$APP_DIR/main/smart_speaker/smart_speaker_dsp.cpp" \
  -o "$WOOD_FISH_TEST_BIN"
"$WOOD_FISH_TEST_BIN"

node "$APP_DIR/test/music_metadata_test.mjs"

node "$APP_DIR/test/library_refresh_test.mjs"

AMBIENT_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-ambient-test
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I"$APP_DIR/main/smart_speaker" \
  "$APP_DIR/test/ambient_test.cpp" "$APP_DIR/main/smart_speaker/smart_speaker_ambient.cpp" -o "$AMBIENT_TEST_BIN"
"$AMBIENT_TEST_BIN"

python3 "$APP_DIR/test/ambient_upload_test.py"
