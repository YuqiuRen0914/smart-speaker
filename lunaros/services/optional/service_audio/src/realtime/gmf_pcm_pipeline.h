#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "lunaros_audio_effects.h"

namespace lunaredu::audio {

// Private, synchronous PCM16 mono/stereo pipeline. Its owner supplies cancellation,
// focus, bounded admission and task retirement; GMF never creates a mixer task.
class GmfPcmPipeline {
public:
  static constexpr size_t kMaximumFrames = 128;
  enum class Result { Written, Cancelled, Failed };
  using Transform = bool (*)(void *, const int16_t *, int16_t *, size_t);
  using Writer = Result (*)(void *, const int16_t *, size_t);

  GmfPcmPipeline() = default;
  ~GmfPcmPipeline();
  GmfPcmPipeline(const GmfPcmPipeline &) = delete;
  GmfPcmPipeline &operator=(const GmfPcmPipeline &) = delete;

  // All calls are serialized by the existing PCM owner. No callback survives
  // write() or close(); a failed close retains handles for an owner retry.
  // A null transform uses the official format path only. Channel expansion is
  // performed by GMF ch_cvt; frames in callbacks always mean complete frames.
  bool open(uint32_t rate, Transform transform, Writer writer, void *context,
            unsigned input_channels = 1, unsigned output_channels = 0, PcmEffects effects = {});
  Result write(const int16_t *samples, size_t frames);
  bool close();
  bool is_open() const { return stream_ != nullptr; }

private:
  static int write_callback(uint8_t *, uint32_t, void *);
  void *pool_ = nullptr;
  void *render_ = nullptr;
  void *stream_ = nullptr;
  Writer writer_ = nullptr;
  void *context_ = nullptr;
  Result result_ = Result::Failed;
  unsigned input_channels_ = 1, output_channels_ = 1;
  std::array<int16_t, kMaximumFrames * 2> input_{};
};

} // namespace lunaredu::audio
