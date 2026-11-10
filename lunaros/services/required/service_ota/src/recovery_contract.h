#pragma once
#include <cstddef>
#include <cstdint>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if CONFIG_LUNAREDU_OTA_FIELD_DIAGNOSTICS_LAYOUT
#define LUNAROS_RECOVERY_LAYOUT_ID "lunaros-16m-v4-field"
#else
#define LUNAROS_RECOVERY_LAYOUT_ID "lunaros-16m-v3-recovery"
#endif

namespace lunaredu::lunaros::recovery {
inline constexpr char kLayout[] = LUNAROS_RECOVERY_LAYOUT_ID;
#if CONFIG_LUNAREDU_OTA_FIELD_DIAGNOSTICS_LAYOUT
inline constexpr size_t kFilesystemBytes = 0x760000;
#else
inline constexpr size_t kFilesystemBytes = 0x7e0000;
#endif
inline constexpr char kDualLayout[] = "lunaros-16m-v2";
inline constexpr uint32_t kProtocol = 1;
inline constexpr size_t kContractOffset = 24 + 8 + 256;
enum class Role : uint32_t { Recovery = 1, Business = 2 };
struct ImageContract {
  char magic[8];
  uint32_t protocol;
  Role role;
  char layout[32];
  char hardware[32];
};
static_assert(sizeof(ImageContract) == 80);
enum class Phase : uint32_t { Idle, Requested, Writing, BootPending, Confirmed, Failed, Rescue,
                              BootUpdated };
struct Record {
  uint32_t magic = 0x3352434c;
  uint32_t schema = kProtocol;
  uint32_t bytes = sizeof(Record);
  uint32_t generation = 0;
  Phase phase = Phase::Idle;
  uint32_t image_bytes = 0;
  uint8_t image_sha256[32] = {};
  uint32_t crc = 0;
};
static_assert(sizeof(Record) == 60);
uint32_t checksum(const void *data, size_t size);
void seal(Record &record);
bool valid(const Record &record);
const Record *latest(const Record *a, const Record *b);
const char *phase_name(Phase phase);
bool image_contract_allowed(const uint8_t *data, size_t size,
                            const char *hardware, const char *&error,
                            const char *layout = kLayout, Role role = Role::Business);
}
