#!/bin/sh
set -eu
APP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CXX=${CXX:-c++}
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
CHARACTER_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-character-test
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 \
  -I"$APP_DIR/main/smart_speaker" -I"$APP_DIR/components/lunaredu_character_assets/include" \
  "$APP_DIR/test/character_test.cpp" \
  "$APP_DIR/main/smart_speaker/smart_speaker_character.cpp" \
  -o "$CHARACTER_TEST_BIN"
"$CHARACTER_TEST_BIN"
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

PAUSE_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-pause-indicator-test
"$CXX" -std=c++17 -Wall -Wextra -Werror -O2 -I"$APP_DIR/main/smart_speaker" -I"$APP_DIR/components/lunaredu_character_assets/include" "$APP_DIR/test/pause_indicator_test.cpp" -o "$PAUSE_TEST_BIN"
"$PAUSE_TEST_BIN"

BOOT_TEST_BIN=${TMPDIR:-/tmp}/smart-speaker-boot-confirmation-test
"$CXX" -std=c++20 -Wall -Wextra -Werror -I"$APP_DIR/lunaros/include" "$APP_DIR/test/boot_confirmation_test.cpp" -o "$BOOT_TEST_BIN"
"$BOOT_TEST_BIN"
for name in audio_lifecycle transfer lyrics_api track_remove local_menu playback_mode mp3_guard; do
    python3 "$APP_DIR/test/${name}_test.py"
done

python3 "$APP_DIR/test/avatar_test.py"
python3 "$APP_DIR/test/input_budget_test.py"
node "$APP_DIR/test/web_api_test.mjs"
