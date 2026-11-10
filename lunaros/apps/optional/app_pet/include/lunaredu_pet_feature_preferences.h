#pragma once
#include <cstdint>
#include <string>
namespace lunaredu::pet {
enum class Feature : uint8_t { Flexible=0, Imu=1, Voice=2, Agent=3 };
struct FeaturePreferences {
  bool flexible=true,imu=true,voice=true,agent=true;
  uint8_t bits() const {return unsigned(flexible)|(unsigned(imu)<<1)|(unsigned(voice)<<2)|(unsigned(agent)<<3);}
  bool enabled(Feature f) const {return (bits()&(1U<<unsigned(f)))!=0;}
  static FeaturePreferences from_bits(uint8_t b){return {bool(b&1),bool(b&2),bool(b&4),bool(b&8)};}
};
FeaturePreferences feature_preferences();
void load_feature_preferences();
bool set_feature_enabled(Feature,bool,std::string &error);
// Local diagnostics only: bounded override, never writes user preferences.
bool set_diagnostic_features(FeaturePreferences,uint32_t duration_ms,std::string &error);
void reset_diagnostic_features();
uint32_t diagnostic_features_remaining_ms();
}
