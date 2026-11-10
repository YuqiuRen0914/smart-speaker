#pragma once

#include <string>

#include "lunaredu_lunaros/device_api/device_projection.h"

namespace lunaredu::lunaros {

// Internal composition helpers used only by the projection registry. Model
// storage itself lives under model and has no JSON dependency.
void setup_device_capability_index();
void setup_device_metadata_index();

bool register_device_metadata_index(
    const DeviceMetadataProjection &contribution,
    std::string *error = nullptr,
    bool (*commit)(void *, std::string *) = nullptr, void *context = nullptr);

bool seal_device_metadata_index(std::string *error = nullptr);
bool device_metadata_index_sealed();

} // namespace lunaredu::lunaros
