#pragma once
#include "lunaros_audio_effects.h"
#include <cstddef>
#include <cstdint>
#include <memory>
namespace lunaredu::audio {
struct PcmOutputPorts {
  void *context=nullptr;
  // Zero follows the source. Fixed-rate conversion currently admits mono sinks.
  unsigned sample_rate=0,channels=0;
  bool (*prepare)(void *,unsigned sample_rate,unsigned channels)=nullptr;
  bool (*write)(void *,const int16_t *,size_t frames,unsigned rate,unsigned channels)=nullptr;
  PcmEffects effects{};
};
// Synchronous format/partition adapter. GMF performs channel conversion;
// Audio Effects performs rate conversion through the fixed-quantum adapter.
// No task, focus broker, filesystem handle or second PCM queue is created.
class PcmOutputAdapter {
public:
  PcmOutputAdapter();
  ~PcmOutputAdapter();
  PcmOutputAdapter(const PcmOutputAdapter &)=delete;
  PcmOutputAdapter &operator=(const PcmOutputAdapter &)=delete;
  bool configure(PcmOutputPorts);
  bool write(const int16_t *,size_t frames,unsigned rate,unsigned channels);
  bool write_bytes(const uint8_t *,size_t bytes,unsigned rate,unsigned channels);
  bool close(); // False retains callbacks/renderer; owner must retry before destruction.
  const char *error() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
