#pragma once
#include <cstdint>

namespace lunaredu::lunaros::task_signal {
// Slot zero belongs to SDK/driver protocols. OS owners use one coalescing
// wake slot; payloads and completion acknowledgements remain in their queues
// and semaphores. The caller must retain the task through notification.
inline constexpr uint32_t kWaitForever = UINT32_MAX;
void notify(void *task);

// A wake given while the owner works is retained until the next wait. Taking
// only this slot cannot consume a driver's in-flight completion notification.
// Zero polls; finite waits use milliseconds, rounded to at least one SDK tick.
uint32_t wait_ms(uint32_t timeout_ms);
} // namespace lunaredu::lunaros::task_signal
