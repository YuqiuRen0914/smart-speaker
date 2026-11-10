#pragma once
#include "lunaredu_music_player.h"
#include <cstdio>
#include <memory>
#include <string>
namespace lunaredu::music {
// The immutable library is retained, not copied per frame. No Bluetooth
// discovery array is captured and unrelated discovery revisions do not redraw.
class PresentationSnapshotCache {
 public:
  std::shared_ptr<const MusicSnapshot> capture(const std::string &error) {
    auto snapshot=music_snapshot();
    if(!error.empty())std::snprintf(snapshot.error.data(),snapshot.error.size(),"%s",error.c_str());
    if(!value_||*value_!=snapshot)value_=std::make_shared<MusicSnapshot>(snapshot);
    return value_;
  }
 private:
  std::shared_ptr<const MusicSnapshot> value_;
};
}
