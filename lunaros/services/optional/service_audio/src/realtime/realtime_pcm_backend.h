#pragma once

#include <memory>
#include <string>

#include "pcm_backend.h"

namespace lunaredu::lunaros {
class KernelRuntime;
}

namespace lunaredu::audio {

// Wraps a hardware PCM backend with the bounded fixed-block output pipeline
// proven by the Xiaoxing full-duplex audio path. The hardware backend remains
// responsible for clocks, DMA and sample-slot conversion.
std::unique_ptr<PcmBackend> create_realtime_pcm_backend(
    std::unique_ptr<PcmBackend> hardware,
    lunaredu::lunaros::KernelRuntime &runtime,
    const char *owner_service_id, std::string &error);

} // namespace lunaredu::audio
