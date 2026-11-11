#pragma once

#include <cstddef>
#include <string>

#include <ArduinoJson.h>

namespace smart_speaker {

// Narrow read-only projection used by the assistant MCP status tool. The
// latest runtime error is intentionally not correlated with a particular
// command; callers must treat it as device context, not a command receipt.
struct PlaybackToolStatus {
  bool audio_ready = false;
  bool playing = false;
  bool paused = false;
  bool uploading = false;
  int volume = 0;
  const std::string &track;
  size_t playlist_count = 0;
  const std::string &last_error;
};

inline void fill_playback_tool_status(JsonObject payload,
                                      const PlaybackToolStatus &status) {
  payload["audio_ready"] = status.audio_ready;
  payload["playing"] = status.playing;
  payload["paused"] = status.paused;
  payload["uploading"] = status.uploading;
  payload["volume"] = status.volume;
  payload["track"] = status.track;
  payload["playlist_count"] = status.playlist_count;
  payload["last_error"] = status.last_error;
}

} // namespace smart_speaker
