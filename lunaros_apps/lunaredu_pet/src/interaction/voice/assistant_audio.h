#pragma once

#include <cstddef>
#include <cstdint>

#include "interaction/conversation/assistant_runtime_types.h"

namespace lunaredu::pet::conversation::internal {

void observe_vad_frame_locked(const int16_t *samples, size_t sample_count, uint32_t frame_ms);
bool process_uplink_audio_work_item(const std::vector<int16_t> &samples);
bool prime_uplink_audio_channel();
bool poll_local_audio_once();
void audio_task(void *argument);
// Serialize a listening-state transition with the bounded I2S read and
// uplink encode/send operation. The caller must hold this only around the
// transition; it must not be used from audio_task itself.

} // namespace lunaredu::pet::conversation::internal

namespace lunaredu::pet::conversation::internal {
// Call only after publishing service suspension; waits for bounded I/O to finish.
void quiesce_audio();
}
