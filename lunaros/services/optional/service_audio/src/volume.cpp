#include "lunaros_audio_volume.h"
#include "configuration/settings.h"
#include <mutex>
#include <charconv>
#include <string>
namespace lunaredu::audio {
namespace {
std::mutex mutex;
VolumeOutputPorts output;
bool loaded = false;
uint8_t volume = 70;
uint8_t default_volume = 70;
void load() {
  if (loaded) return;
  std::string text, error;
  const auto result = lunaros::settings_read_string("lunar_os", "speaker_volume", 16,
                                                   text, error);
  if (result == lunaros::SettingsReadResult::Failed) return;
  if (result == lunaros::SettingsReadResult::Missing) text = std::to_string(default_volume);
  unsigned value = default_volume;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value <= 100)
    volume = static_cast<uint8_t>(value);
  loaded = true;
}
}
void configure_volume_output(VolumeOutputPorts ports) {
  std::lock_guard<std::mutex> lock(mutex);
  output = ports;
  if (!loaded && ports.default_volume_percent <= 100) {
    default_volume = ports.default_volume_percent;
    volume = default_volume;
  }
  load();
}
uint8_t volume_percent() {
  std::lock_guard<std::mutex> lock(mutex); load(); return volume;
}
bool set_volume_percent(uint8_t percent) {
  bool applied=false;
  return save_volume_percent(percent,applied)&&applied;
}
bool save_volume_percent(uint8_t percent, bool &applied) {
  applied=false;
  if (percent > 100) return false;
  std::lock_guard<std::mutex> lock(mutex); load(); volume = percent;
  const bool persisted = lunaros::settings_set_string("lunar_os", "speaker_volume", std::to_string(percent));
  applied = output.set_volume && output.set_volume(percent);
  return persisted;
}
bool apply_volume() {
  std::lock_guard<std::mutex> lock(mutex); load();
  return output.set_volume && output.set_volume(volume);
}
}
