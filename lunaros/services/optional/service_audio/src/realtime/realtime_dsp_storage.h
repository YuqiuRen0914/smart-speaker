#pragma once

#include "lunaros_audio_dsp.h"
#include <memory>
#include <new>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace lunaredu::audio::internal {
struct RealtimeDspDeleter {
  void operator()(dsp::AudioProcessor *processor) const {
    if (!processor) return;
#ifdef ESP_PLATFORM
    processor->~AudioProcessor();
    heap_caps_free(processor);
#else
    delete processor;
#endif
  }
};
using RealtimeDspStorage = std::unique_ptr<dsp::AudioProcessor, RealtimeDspDeleter>;
inline RealtimeDspStorage make_realtime_dsp() {
#ifdef ESP_PLATFORM
  // The queue-bearing backend can live in PSRAM. Keep only its small,
  // per-sample mutable DSP state in SRAM; never fall back silently to PSRAM.
  void *storage = heap_caps_malloc(sizeof(dsp::AudioProcessor),
                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!storage) return {};
  return RealtimeDspStorage{new (storage) dsp::AudioProcessor};
#else
  return RealtimeDspStorage{new (std::nothrow) dsp::AudioProcessor};
#endif
}
} // namespace lunaredu::audio::internal
