#pragma once

namespace lunaredu::pet::conversation::internal {

void setup_assistant_minimal_app();
void tick_assistant_minimal_app();
bool assistant_worker_tasks_ready();
// Request ESP-SR teardown from an internal-RAM task.  Callers may run on a
// PSRAM-backed task stack and must not invoke WakeWordFrontend::release()
// directly because ESP-IDF freezes the flash cache while unmapping models.
bool request_wake_word_frontend_release();

} // namespace lunaredu::pet::conversation::internal
