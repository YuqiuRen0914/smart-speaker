#pragma once

#include <cstdint>
#include <string>

#include <ArduinoJson.h>

namespace lunaredu::lunaros::platform_link {

inline constexpr uint32_t kPlatformLinkTimeSyncTimeoutMs = 6000;

uint32_t monotonic_ms();
bool platform_is_production();
std::string platform_base_url();
bool ensure_network_clock(std::string &error);
void configure_resource_transport();

bool perform_request(const char *method, const std::string &path,
                     const std::string *body, JsonDocument *response_doc,
                     int *status_code = nullptr);
bool perform_public_request(const char *method, const std::string &path,
                            const std::string *body,
                            JsonDocument *response_doc,
                            int *status_code = nullptr);

} // namespace lunaredu::lunaros::platform_link
