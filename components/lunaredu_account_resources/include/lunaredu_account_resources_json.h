#pragma once

#include "lunaredu_lunaros/device_link/device_link_json.h"

namespace lunaredu::account_resources {

// Product composition keeps the returned observer alive for the service
// lifetime. Views use account_resources.h and do not depend on this cloud codec
// boundary.
lunaros::DeviceLinkObserver
observer(void (*write_runtime)(JsonObject) = nullptr);

} // namespace lunaredu::account_resources
