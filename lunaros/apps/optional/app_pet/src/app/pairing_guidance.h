#pragma once
#include <cstdint>
#include <string>

namespace lunaredu::pet {
const std::string &pairing_guidance_caption(bool enabled, uint64_t now_ms);
bool stop_pairing_guidance(std::string &error);
}
