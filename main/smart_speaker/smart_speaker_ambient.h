#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

namespace smart_speaker::ambient {
constexpr int kSceneCount = 4;
inline constexpr const char *kIds[] = {"rain", "shore", "forest", "cafe"};
inline constexpr const char *kNames[] = {"雨窗", "轻柔潮水", "森林", "咖啡馆"};
inline bool valid_scene(int scene) { return scene >= 0 && scene < kSceneCount; }
inline bool valid_minutes(int minutes) { return minutes == 0 || minutes == 30 || minutes == 60 || minutes == 90; }
// Owned by Runtime under its mutex. A timer survives page/scene/voice changes,
// but not reboot or an explicit source stop. Unsigned subtraction handles wrap.
struct Session {
  int scene = -1;
  int preview = 0;
  bool choosing = true;
  bool timer_menu = false;
  uint32_t overlay_at = 0;
  bool overlay = false;
  uint32_t timer_started = 0;
  uint32_t timer_duration = 0;
  void reveal(uint32_t now) { overlay = true; overlay_at = now; }
  bool visible(uint32_t now) const { return choosing || timer_menu || (overlay && uint32_t(now-overlay_at) < 3500); }
  uint32_t remaining(uint32_t now) const {
    const auto elapsed = uint32_t(now - timer_started);
    return elapsed >= timer_duration ? 0 : timer_duration - elapsed;
  }
  bool expired(uint32_t now) const { return timer_duration && !remaining(now); }
  bool set_timer(int minutes, uint32_t now) {
    if (!valid_minutes(minutes)) return false;
    timer_started = now; timer_duration = uint32_t(minutes) * 60000;
    timer_menu = false; reveal(now); return true;
  }
  void cancel_timer() { timer_duration = 0; timer_menu = false; }
  int shown_scene() const { return choosing ? preview : std::max(0, scene); }
};

// A seekable, continuously looping PCM source in the existing WAV decoder.
// Only this source is attenuated by the sleep timer; assistant audio is untouched.
FILE *open_loop(const std::string &path, std::string &error);
bool validate_loop_file(const std::string &path);
void set_gain(unsigned permille);
bool take_read_error();
} // namespace smart_speaker::ambient
