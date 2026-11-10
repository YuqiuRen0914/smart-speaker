#pragma once
#include <cstdint>
#include "app/package/save_format_limits.h"
#include <string>
#include <vector>
namespace lunaredu::lunaros::apps {
// Shared checksum for the portable envelope and its persistent metadata.
uint32_t save_hash(const uint8_t *data, size_t size);
bool encode_save_file(const std::string &id,
                             const std::vector<uint8_t> &data,
                             uint32_t timestamp, std::vector<uint8_t> &out,
                             size_t maximum_save_bytes);
bool decode_save_file(const std::string &id, const uint8_t *p, size_t n,
                             std::vector<uint8_t> &data, size_t maximum_save_bytes);
} // namespace lunaredu::lunaros::apps
