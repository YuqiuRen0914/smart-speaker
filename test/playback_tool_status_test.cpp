#include "smart_speaker_tool_status.h"

#include <cassert>
#include <iostream>

int main() {
  using namespace smart_speaker;
  const std::string track = "evening.mp3";
  const std::string error = "track_open_failed";
  PlaybackToolStatus status{true, false, false, false, 18, track, 5, error};
  JsonDocument output;
  fill_playback_tool_status(output.to<JsonObject>(), status);

  JsonDocument decoded;
  std::string encoded;
  serializeJson(output, encoded);
  assert(deserializeJson(decoded, encoded) == DeserializationError::Ok);
  assert(decoded["audio_ready"] == true);
  assert(decoded["playing"] == false);
  assert(decoded["paused"] == false);
  assert(decoded["uploading"] == false);
  assert(decoded["volume"] == 18);
  assert(decoded["track"] == "evening.mp3");
  assert(decoded["playlist_count"] == 5);
  assert(decoded["last_error"] == "track_open_failed");
  assert(decoded["command_id"].isNull());

  const std::string no_error;
  PlaybackToolStatus healthy{true, true, false, false, 18, track, 5, no_error};
  output.clear();
  fill_playback_tool_status(output.to<JsonObject>(), healthy);
  assert(output["playing"] == true);
  assert(output["last_error"] == "");
  std::cout << "Assistant playback status exposes observed state without inventing command receipts\n";
}
