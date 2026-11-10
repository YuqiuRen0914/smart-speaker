#pragma once
#include <cstddef>
#include <cstdint>

namespace lunaredu::lunaros {
struct NetworkJobPolicy {
  size_t maximum_active_jobs = 12;
  size_t maximum_jobs_per_owner = 4;
  uint8_t ageing_boost_per_dispatch = 8;
  uint8_t maximum_ageing_boost = 255;
  uint32_t default_retry_backoff_ms = 1000;
  uint32_t maximum_retry_backoff_ms = 30000;
};
}
