#include "smart_speaker_audio_read_guard.h"
#include "audio_mp3.h"
#include <atomic>
#include <cstdio>

namespace { std::atomic<bool> read_error{false}; }
namespace smart_speaker { bool dual_i2s_audio_drain(uint32_t timeout_ms); }

// CMake renames only the dependency's implementation; its caller resolves to
// this adapter. Do not modify generated managed_components sources.
DECODE_STATUS speaker_decode_mp3_unchecked(HMP3Decoder, FILE *, decode_data *, mp3_instance *);
DECODE_STATUS decode_mp3(HMP3Decoder decoder, FILE *file, decode_data *output, mp3_instance *instance) {
  const auto result = speaker_decode_mp3_unchecked(decoder, file, output, instance);
  if (std::ferror(file)) {
    read_error.store(true, std::memory_order_release);
    return DECODE_STATUS_ERROR;
  }
  if (result == DECODE_STATUS_DONE) (void)smart_speaker::dual_i2s_audio_drain(1500);
  return result;
}
void speaker_audio_read_error_reset() { read_error.store(false, std::memory_order_release); }
bool speaker_audio_read_error_take() { return read_error.exchange(false, std::memory_order_acq_rel); }
