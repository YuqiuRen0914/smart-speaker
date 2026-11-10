#pragma once

// Registers the versioned Device API. The API exposes device and module
// resources over HTTP; it does not expose internal executor or transport
// scheduling details.
void register_device_api_routes();
