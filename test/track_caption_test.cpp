#include "smart_speaker_track_caption.h"
#include <cassert>
#include <cstdio>

int main() {
  for (uint32_t start : {0U, 0xfffffff0U}) {
    smart_speaker::TrackCaption caption;
    assert(caption.update(start, "", -1, false, false).text == "待播放");
    auto result = caption.update(start + 100, "晴天.MP3", 0, true, false);
    assert(result.entered && result.text == "正在播放 · 晴天");
    assert(!caption.update(start + 140, "晴天.MP3", 0, true, false).entered);
    assert(caption.update(start + 5099, "晴天.MP3", 0, true, false).text == "正在播放 · 晴天");
    assert(caption.update(start + 5100, "晴天.MP3", 0, true, false).text == "晴天");
    result = caption.update(start + 5200, "夜曲.mp3", 1, true, false);
    assert(result.entered && result.text == "正在播放 · 夜曲");
    assert(caption.update(start + 5250, "夜曲.mp3", 1, false, true).text == "已暂停 · 夜曲");
    assert(caption.update(start + 5300, "夜曲.mp3", 1, false, false).text == "夜曲");
    assert(caption.update(start + 5400, "夜曲.mp3", 1, true, false).entered);
    assert(caption.update(start + 5500, "", -1, false, false).text == "待播放");
  }
  std::puts("track caption: next/previous title, announcement expiry, pause, stop and clock wrap passed");
}
