#include "display/screen_configuration.h"
#include <limits>
namespace lunaredu::display {
bool screen_storage_bytes(uint16_t width,uint16_t height,unsigned frames,
    size_t mailbox_bytes,ScreenStorageBudget budget,size_t &bytes,std::string &error) {
  bytes=0;
  if(!width||!height||!frames||frames>4||!budget.maximum_bytes) {error="screen_storage_budget_invalid";return false;}
  const uint64_t frame_bytes=uint64_t(width)*height*sizeof(uint16_t)*frames;
  if(frame_bytes>std::numeric_limits<size_t>::max() || mailbox_bytes>std::numeric_limits<size_t>::max()-frame_bytes) {error="screen_storage_budget_exceeded";return false;}
  const uint64_t required=frame_bytes+mailbox_bytes;
  if(required>std::numeric_limits<size_t>::max()||budget.reserved_bytes>budget.maximum_bytes||required>budget.maximum_bytes-budget.reserved_bytes) {error="screen_storage_budget_exceeded";return false;}
  bytes=size_t(required);error.clear();return true;
}
}
