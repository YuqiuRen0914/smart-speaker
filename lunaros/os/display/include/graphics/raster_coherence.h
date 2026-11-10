#pragma once
#include "display/pipeline/frame_damage.h"
#include <cstdint>
namespace lunaredu::lunaros::graphics {
// Exclusive target owner records every edit after raster synchronization.
// Unknown writes/rotation/composition invalidate the token. Address reuse alone
// never establishes coherence; identity and complete epoch history are required.
struct RasterCoherence {
  uint64_t owner=0,epoch=0;
  display::FrameDamageMap overwritten;
  void invalidate(){owner=epoch=0;overwritten={};}
};
}
