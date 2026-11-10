#include "app/package/save_format.h"

#include <cstring>
namespace lunaredu::lunaros::apps {
uint32_t save_hash(const uint8_t *p, size_t n) {
  uint32_t h = 2166136261u;
  while (n--)
    h = (h ^ *p++) * 16777619u;
  return h;
}
namespace {
uint32_t save_get32(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
         uint32_t(p[3]) << 24;
}
void save_put32(uint8_t *p, uint32_t n) {
  for (unsigned i = 0; i < 4; i++)
    p[i] = uint8_t(n >> (8 * i));
}
}

bool encode_save_file(const std::string &id,
                             const std::vector<uint8_t> &data,
                             uint32_t timestamp, std::vector<uint8_t> &out,
                             size_t maximum_save_bytes) {
  if (id.size() < 3 || id.size() > kSaveFileIdMaxBytes || data.empty() ||
      data.size() > maximum_save_bytes || data.size() > UINT32_MAX ||
      data.size() > SIZE_MAX - kSaveFileHeaderBytes - id.size())
    return false;
  out.assign(24 + id.size() + data.size(), 0);
  memcpy(out.data(), "LSAV", 4);
  save_put32(out.data() + 4, 1);
  save_put32(out.data() + 8, uint32_t(id.size()));
  save_put32(out.data() + 12, uint32_t(data.size()));
  save_put32(out.data() + 20, timestamp);
  memcpy(out.data() + 24, id.data(), id.size());
  memcpy(out.data() + 24 + id.size(), data.data(), data.size());
  save_put32(out.data() + 16, save_hash(out.data() + 24, out.size() - 24));
  return true;
}

bool decode_save_file(const std::string &id, const uint8_t *p, size_t n,
                             std::vector<uint8_t> &data, size_t maximum_save_bytes) {
  if (!p || n < 28 || n > save_file_maximum_bytes(maximum_save_bytes) || memcmp(p, "LSAV", 4) || save_get32(p + 4) != 1)
    return false;
  const auto a = save_get32(p + 8), b = save_get32(p + 12);
  if (a != id.size() || a < 3 || a > kSaveFileIdMaxBytes || b == 0 || b > maximum_save_bytes ||
      a > n - kSaveFileHeaderBytes || b != n - kSaveFileHeaderBytes - a || memcmp(p + 24, id.data(), a) ||
      save_get32(p + 16) != save_hash(p + 24, n - 24))
    return false;
  data.assign(p + 24 + a, p + n);
  return true;
}
}
