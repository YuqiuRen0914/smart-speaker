#pragma once

#include "lunaredu_lunaros/device_api/device_projection.h"

namespace lunaredu::lunaros {

struct PresentationProjectionConfig {
  DeviceJsonResultProvider snapshot_provider = nullptr;
  DeviceJsonResultProvider state_provider = nullptr;
  DeviceJsonCommandHandler intent_handler = nullptr;
};

// Registers the renderer-neutral Presentation API projection. The product
// composition root supplies the document builder and semantic intent handler;
// LunarOS owns only the stable operation identity and transport metadata.
bool setup_presentation_projection(
    const PresentationProjectionConfig &config);
bool set_presentation_projection_available(bool available,
                                           std::string &error);

} // namespace lunaredu::lunaros
