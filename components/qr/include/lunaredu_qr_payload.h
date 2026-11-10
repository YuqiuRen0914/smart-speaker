#pragma once
#include <string>
#include <string_view>

namespace lunaredu::lunaros::qr {

// Pure Wi-Fi QR encoding: callers own credential selection and payload lifetime.
std::string wifi_payload(std::string_view ssid, std::string_view password);
} // namespace lunaredu::lunaros::qr
