#pragma once
#include <cstdint>
namespace lunaredu::lunaros::boot_guard {
struct Record {
  uint32_t magic, schema;
  uint64_t image;
  uint32_t failures, rescues, armed, crc;
};
bool valid(const Record &record);
void seal(Record &record);
void begin(Record &record, uint64_t image, bool abnormal_reset);
bool rescue_due(const Record &record);
bool admit_rescue(Record &record);
void healthy(Record &record);
}
