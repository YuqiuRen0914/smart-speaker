#pragma once

namespace lunaredu::lunaros {

// Stable physical-device serial derived from the factory MAC. This is the
// hardware/cloud identity and is independent of Local Device API UUIDs or
// third-party protocol client IDs.
const char *hardware_serial_number();

// Firmware version of the currently running image.
const char *firmware_version();

} // namespace lunaredu::lunaros
