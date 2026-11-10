#include "lunaredu_music_playback_mode.h"
#include <algorithm>
namespace lunaredu::music {
int advance_track(PlaybackMode mode, TrackAdvance reason, int current,
                         int count, uint32_t entropy) {
  if (count <= 0) return -1;
  current = std::clamp(current, 0, count - 1);
  if (reason == TrackAdvance::Previous) return (current + count - 1) % count;
  if (reason == TrackAdvance::Finished && mode == PlaybackMode::RepeatOne) return current;
  if (reason == TrackAdvance::Failed && count == 1) return -1;
  if (mode == PlaybackMode::Shuffle && count > 1) {
    const int candidate = entropy % static_cast<uint32_t>(count - 1);
    return candidate >= current ? candidate + 1 : candidate;
  }
  if (reason == TrackAdvance::Finished && mode == PlaybackMode::Sequential && current + 1 == count)
    return -1;
  return (current + 1) % count;
}
}
