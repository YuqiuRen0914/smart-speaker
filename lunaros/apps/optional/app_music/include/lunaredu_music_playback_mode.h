#pragma once
#include <cstdint>

namespace lunaredu::music {
enum class PlaybackMode : uint8_t { Sequential, Shuffle, RepeatOne };
enum class TrackAdvance : uint8_t { Next, Previous, Finished, Failed };
constexpr PlaybackMode next_playback_mode(PlaybackMode mode) {
  return static_cast<PlaybackMode>((static_cast<unsigned>(mode) + 1) % 3);
}
constexpr const char *playback_mode_name(PlaybackMode mode) {
  return mode == PlaybackMode::Shuffle ? "shuffle" :
         mode == PlaybackMode::RepeatOne ? "repeat_one" : "sequential";
}
constexpr const char *playback_mode_label(PlaybackMode mode) {
  return mode == PlaybackMode::Shuffle ? "随机播放" :
         mode == PlaybackMode::RepeatOne ? "单曲重复" : "顺序播放";
}
// Explicit previous always navigates the library backwards; explicit next
// leaves a repeated track. A failed track is never repeated in RepeatOne.
int advance_track(PlaybackMode mode,TrackAdvance reason,int current,int count,uint32_t entropy);

} // namespace lunaredu::music
