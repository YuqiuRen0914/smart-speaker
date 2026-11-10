#include "state/pet_preferences.h"
#include "behavior/appearance_policy.h"
#include "configuration/settings.h"
#include "lunaredu_pet_display_preferences.h"
#include <mutex>
#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include "lunaredu_pet_feature_preferences.h"
#ifdef ESP_PLATFORM
#include <esp_timer.h>
#endif

namespace lunaredu::pet::display_preferences_detail {
namespace {
std::atomic<DisplayMode> mode{DisplayMode::Window};
std::atomic<DisplayModePolicy> policy{DisplayModePolicy::Preference};
std::atomic<bool> loaded{false};
}
}

namespace { std::mutex display_preference_mutex; }

namespace lunaredu::pet::state {
namespace {
constexpr const char *kNamespace = "pet";
constexpr const char *kAppearanceKey = "appearance_v1";

bool decode_appearance(const std::string &value, PetAppearanceState &appearance) {
  if (value == "autonomous") {
    appearance = {};
    return true;
  }
  constexpr const char *kLockedPrefix = "locked|";
  if (value.compare(0, 7, kLockedPrefix) != 0) return false;
  const size_t separator = value.find('|', 7);
  if (separator == std::string::npos) return false;
  const std::string shape = value.substr(7, separator - 7);
  const std::string color = value.substr(separator + 1);
  if ((!shape.empty() && !appearance::valid_shape(shape)) ||
      (!color.empty() && !appearance::valid_color(color)) ||
      (shape.empty() && color.empty())) return false;
  appearance.mode = PetAppearanceMode::Locked;
  appearance.shape = shape;
  appearance.color = color;
  return true;
}
}

void load_appearance_preferences(PetState &state) {
  load_feature_preferences();
  std::string encoded, error;
  if(!display_preferences_detail::loaded.load(std::memory_order_acquire)){
    std::lock_guard<std::mutex> lock(display_preference_mutex);
    if(!display_preferences_detail::loaded.load(std::memory_order_acquire)){
    bool fullscreen=display_mode()==DisplayMode::Fullscreen;
    if(lunaredu::lunaros::settings_read_bool(kNamespace,"fullscreen",fullscreen,error)!=lunaredu::lunaros::SettingsReadResult::Failed){
      display_preferences_detail::mode.store(fullscreen?DisplayMode::Fullscreen:DisplayMode::Window,std::memory_order_release);
      display_preferences_detail::loaded.store(true,std::memory_order_release);
    }
    }
  }
  if (lunaredu::lunaros::settings_read_string(kNamespace, kAppearanceKey, 256,
                                              encoded, error) ==
      lunaredu::lunaros::SettingsReadResult::Failed) return;
  state.appearance = {};
  (void)decode_appearance(encoded, state.appearance);
}

bool persist_appearance_preferences(const PetState &state) {
  const auto &value = state.appearance;
  const std::string encoded = value.mode == PetAppearanceMode::Locked
      ? "locked|" + value.shape + "|" + value.color : "autonomous";
  return lunaredu::lunaros::settings_set_string(kNamespace, kAppearanceKey,
                                                encoded);
}
} // namespace lunaredu::pet::state

namespace lunaredu::pet {
bool set_display_mode(DisplayMode mode,std::string &error){
  std::lock_guard<std::mutex> lock(display_preference_mutex);
  if(mode!=DisplayMode::Window&&mode!=DisplayMode::Fullscreen){error="pet_display_mode_invalid";return false;}
  if(display_mode_policy()==DisplayModePolicy::FullscreenOnly&&mode!=DisplayMode::Fullscreen){error="pet_display_mode_fixed_fullscreen";return false;}
  if(!lunaredu::lunaros::settings_set_bool("pet","fullscreen",mode==DisplayMode::Fullscreen)){error="pet_display_preference_save_failed";return false;}
  display_preferences_detail::mode.store(mode,std::memory_order_release);
  display_preferences_detail::loaded.store(true,std::memory_order_release);error.clear();return true;
}
}

namespace lunaredu::pet {
namespace {
std::atomic<uint8_t> feature_bits{15},diagnostic_bits{15};
std::atomic<uint32_t> diagnostic_until{0};
bool features_loaded=false;
constexpr std::array<const char *,4> feature_keys={"flexible","imu","voice","agent"};
uint32_t feature_now_ms(){
#ifdef ESP_PLATFORM
 return uint32_t(esp_timer_get_time()/1000);
#else
 return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
}
FeaturePreferences feature_preferences(){
 const auto until=diagnostic_until.load(std::memory_order_acquire);
 return FeaturePreferences::from_bits(until && int32_t(until-feature_now_ms())>0
     ?diagnostic_bits.load():feature_bits.load(std::memory_order_acquire));
}
void load_feature_preferences(){
 std::lock_guard<std::mutex> lock(display_preference_mutex);
 if(features_loaded)return;
 uint8_t bits=feature_bits.load();bool okay=true;
 for(unsigned i=0;i<feature_keys.size();++i){bool enabled=(bits&(1U<<i))!=0;std::string error;
  const auto result=lunaros::settings_read_bool("pet",feature_keys[i],enabled,error);
  if(result==lunaros::SettingsReadResult::Failed){okay=false;continue;}
  if(result!=lunaros::SettingsReadResult::Found)continue;
  bits=enabled?bits|(1U<<i):bits&~(1U<<i);
 }
 feature_bits.store(bits,std::memory_order_release);features_loaded=okay;
}
bool set_feature_enabled(Feature feature,bool enabled,std::string &error){
 const auto i=unsigned(feature);if(i>=feature_keys.size()){error="pet_feature_invalid";return false;}
 std::lock_guard<std::mutex> lock(display_preference_mutex);
 auto bits=feature_bits.load();if(bool(bits&(1U<<i))==enabled){error.clear();return true;}
 if(!lunaros::settings_set_bool("pet",feature_keys[i],enabled)){error="pet_feature_save_failed";return false;}
 bits=enabled?bits|(1U<<i):bits&~(1U<<i);feature_bits.store(bits,std::memory_order_release);error.clear();return true;
}
bool set_diagnostic_features(FeaturePreferences p,uint32_t duration,std::string &error){
 if(duration<1000||duration>300000){error="pet_diagnostic_duration_invalid";return false;}
 diagnostic_bits.store(p.bits());diagnostic_until.store(feature_now_ms()+duration,std::memory_order_release);error.clear();return true;
}
void reset_diagnostic_features(){diagnostic_until.store(0,std::memory_order_release);}
uint32_t diagnostic_features_remaining_ms(){const auto until=diagnostic_until.load(std::memory_order_acquire);const int32_t remaining=until?int32_t(until-feature_now_ms()):0;return remaining>0?uint32_t(remaining):0;}
}

namespace lunaredu::pet {
void configure_display_mode_policy(DisplayModePolicy policy){display_preferences_detail::policy.store(policy,std::memory_order_release);}
DisplayModePolicy display_mode_policy(){return display_preferences_detail::policy.load(std::memory_order_acquire);}
DisplayMode display_mode(){return display_mode_policy()==DisplayModePolicy::FullscreenOnly?DisplayMode::Fullscreen:display_preferences_detail::mode.load(std::memory_order_acquire);}
void default_display_mode(DisplayMode mode){if(!display_preferences_detail::loaded.load(std::memory_order_acquire))display_preferences_detail::mode.store(mode,std::memory_order_release);}

int body_diameter(int width,int height){return std::max(1,std::min(width,height)*(display_mode()==DisplayMode::Fullscreen?2:5)/(display_mode()==DisplayMode::Fullscreen?3:12));}
}
