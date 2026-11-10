#include "recovery_contract.h"
#include "lunaros_crc32.h"
#include <cstring>
#include <limits>

namespace lunaredu::lunaros::recovery {
uint32_t checksum(const void *data, size_t size) {
  return digest::crc32(data,size);
}
void seal(Record &record) { record.crc = checksum(&record, offsetof(Record, crc)); }
bool valid(const Record &record) {
  return record.magic == 0x3352434c && record.schema == kProtocol &&
      record.bytes == sizeof(Record) && record.generation > 0 &&
      record.generation < std::numeric_limits<uint32_t>::max() &&
      static_cast<uint32_t>(record.phase) <= static_cast<uint32_t>(Phase::BootUpdated) &&
      record.image_bytes <= 0x600000 &&
      record.crc == checksum(&record, offsetof(Record, crc));
}
const Record *latest(const Record *a, const Record *b) {
  if (!a) return b;
  if (!b) return a;
  // Equal generations with different contents cannot be reconciled safely.
  if (a->generation == b->generation && std::memcmp(a, b, sizeof(Record))) return nullptr;
  return a->generation >= b->generation ? a : b;
}
const char *phase_name(Phase phase) {
  switch (phase) {
  case Phase::Idle: return "idle";
  case Phase::Requested: return "requested";
  case Phase::Writing: return "writing";
  case Phase::BootPending: return "boot_pending";
  case Phase::Confirmed: return "confirmed";
  case Phase::Failed: return "failed";
  case Phase::Rescue: return "rescue";
  case Phase::BootUpdated: return "boot_updated";
  }
  return "invalid";
}
bool image_contract_allowed(const uint8_t *data, size_t size,
                            const char *hardware, const char *&error, const char *layout, Role role) {
  error = "ota_v3_contract_missing";
  if (!data || size < kContractOffset + sizeof(ImageContract) || data[0] != 0xe9) return false;
  ImageContract candidate{};
  std::memcpy(&candidate, data + kContractOffset, sizeof(candidate));
  if (std::memcmp(candidate.magic, "LUNARV3", 8) || candidate.protocol != kProtocol) return false;
  error = "ota_v3_layout_mismatch";
  if (!layout || std::strlen(layout) >= sizeof(candidate.layout) ||
      std::memcmp(candidate.layout, layout, std::strlen(layout) + 1)) return false;
  error = role == Role::Business ? "ota_v3_business_image_required" : "ota_boot_image_required";
  if (candidate.role != role) return false;
  error = "ota_v3_hardware_mismatch";
  if (!hardware || std::strlen(hardware) >= sizeof(candidate.hardware) ||
      std::memcmp(candidate.hardware, hardware, std::strlen(hardware) + 1)) return false;
  error = nullptr;
  return true;
}
}
