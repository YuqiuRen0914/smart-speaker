#pragma once

#include <cstdint>
#include <string>

namespace smart_speaker {

// Presentation only: never edits SD filenames or the playback queue.
class TrackCaption {
 public:
  struct Result { std::string text; bool entered = false; };
  Result update(uint32_t now, const std::string &name, int index,
                bool playing, bool paused) {
    const bool changed = name != name_ || index != index_;
    const bool entered = playing && !name.empty() && (changed || !playing_);
    if (entered) { announced_ms_ = now; announcing_ = true; }
    if (!playing || now - announced_ms_ >= 5000) announcing_ = false;
    name_ = name;
    index_ = index;
    playing_ = playing;
    std::string title = name;
    if (title.size() >= 4) {
      const auto ext = title.substr(title.size() - 4);
      if (ext == ".mp3" || ext == ".MP3" || ext == ".wav" || ext == ".WAV")
        title.resize(title.size() - 4);
    }
    if (title.empty()) return {"待播放", false};
    return {(paused ? "已暂停 · " : announcing_ ? "正在播放 · " : "") + title, entered};
  }

 private:
  std::string name_;
  int index_ = -1;
  uint32_t announced_ms_ = 0;
  bool announcing_ = false;
  bool playing_ = false;
};

}  // namespace smart_speaker
