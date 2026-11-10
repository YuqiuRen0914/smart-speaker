#pragma once
#include <cstdint>

namespace lunaredu::lunaros {
// Sensor polling keeps its original sampling grid after an overrun, skipping
// expired slots. It never replays readings or spins to catch up. The returned
// base is for vTaskDelayUntil(&base, period); every call yields 1..period ticks.
// Unlike control loops below, completion time does not move the entire grid.
inline uint32_t periodic_sample_delay_base(uint32_t now, uint32_t wake,
                                           uint32_t period) {
  if (!period) return now;
  const uint32_t elapsed = now - wake;
  if (elapsed >= 0x80000000u) return now;
  const uint32_t delay = period - elapsed % period;
  return now - period + delay;
}

// Rebase after a missed deadline. Never replay stale control iterations in a
// tight catch-up loop; the next delay still releases the CPU for at least a tick.
inline uint32_t periodic_delay_base(uint32_t now, uint32_t wake,
                                        uint32_t period, uint32_t missed_delay = 0) {
  const uint32_t elapsed = now - wake;
  if (elapsed < period || elapsed >= 0x80000000u) return wake;
  // Driving/receiver loops keep a full period by default. Video foregrounds
  // can yield one tick after an overrun without replaying old iterations.
  const uint32_t delay = missed_delay && missed_delay < period ? missed_delay : period;
  return now - period + delay;
}
} // namespace lunaredu::lunaros
