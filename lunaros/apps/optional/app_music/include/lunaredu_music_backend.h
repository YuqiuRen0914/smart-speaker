#pragma once
#include "lunaredu_music_player.h"
namespace lunaredu::music {
// Product composition binds one retained playback provider before declaration.
// The App owns navigation/presentation; shared audio services own decode/DSP/I/O.
// Callbacks run on the existing main executor and must not retain request strings.
struct PlayerBackend {
  void *context = nullptr;
  MusicSnapshot (*snapshot)(void *) = nullptr;
  bool (*refresh)(void *, std::string &) = nullptr;
  bool (*select)(void *, const Track &, std::string &) = nullptr;
  bool (*action)(void *, uint32_t, std::string &) = nullptr;
  bool (*stop)(void *, std::string &) = nullptr;
  // A service-owned backend may retain playback after its display is released.
  // Its service must stop/retire audio for maintenance and shutdown.
  bool background_playback=false;
  PlaybackActivity (*activity)(void *) = nullptr;
};
bool configure_player_backend(PlayerBackend, std::string &error);
}
