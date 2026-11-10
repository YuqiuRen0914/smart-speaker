#include "lunaredu_lunaros/persistence/system_audio.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include <mutex>
#include <charconv>
#include <string>
namespace lunaredu::lunaros {
namespace {
std::mutex mutex;
AudioOutputPorts output;
bool loaded = false;
uint8_t volume = 70;
void load() {
  if (loaded) return;
  const auto text = settings_get_string("lunar_os", "speaker_volume", "70");
  unsigned value = 70;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value <= 100)
    volume = static_cast<uint8_t>(value);
  loaded = true;
}
}
void configure_audio_output(AudioOutputPorts ports) {
  std::lock_guard<std::mutex> lock(mutex); output = ports; load();
}
uint8_t system_volume_percent() {
  std::lock_guard<std::mutex> lock(mutex); load(); return volume;
}
bool set_system_volume_percent(uint8_t percent) {
  if (percent > 100) return false;
  std::lock_guard<std::mutex> lock(mutex); load(); volume = percent;
  const bool persisted = settings_set_string("lunar_os", "speaker_volume", std::to_string(percent));
  const bool applied = output.set_volume && output.set_volume(percent);
  return persisted && applied;
}
bool apply_system_volume() {
  std::lock_guard<std::mutex> lock(mutex); load();
  return output.set_volume && output.set_volume(volume);
}
}
