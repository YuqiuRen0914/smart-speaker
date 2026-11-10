#pragma once

#include <stdint.h>

namespace lunaredu::lunaros {

// Internal bridge used by the HTTPS source implementation. Public callers observe
// these transitions through ota_service_status_snapshot().
void ota_service_report_pull_queued(uint32_t job_id);
void ota_service_report_pull_started(uint32_t job_id);
void ota_service_report_pull_finished(uint32_t job_id);

} // namespace lunaredu::lunaros
