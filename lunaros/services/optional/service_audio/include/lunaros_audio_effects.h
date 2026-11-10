#pragma once
#include <cstdint>
namespace lunaredu::audio {
// Optional source processing, before the existing speaker protection, exact
// volume curve and cancellation fade. Defaults preserve the original PCM.
struct PcmEffects {
  bool compressor = false;
  int8_t gain_db = 0; // attenuation only; never bypass the product volume ceiling
  uint16_t fade_in_ms = 0;
  // Optional source tone shaping: attenuation only, before speaker protection.
  // Bands are 120 Hz low shelf, 1 kHz peak, min(8 kHz, 0.4*rate) high shelf.
  int8_t low_db=0,mid_db=0,high_db=0;
};
constexpr bool valid_pcm_effects(PcmEffects effects) {
  return effects.gain_db >= -64 && effects.gain_db <= 0 && effects.fade_in_ms <= 5000
      &&effects.low_db>=-15&&effects.low_db<=0&&effects.mid_db>=-15&&effects.mid_db<=0
      &&effects.high_db>=-15&&effects.high_db<=0;
}
}
