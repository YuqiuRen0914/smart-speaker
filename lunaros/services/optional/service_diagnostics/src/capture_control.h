#pragma once
namespace lunaredu::lunaros { class KernelRuntime; }
namespace lunaredu::system { void poll_serial_diagnostics(lunaredu::lunaros::KernelRuntime &); }
#include "diagnostics/backend.h"
namespace lunaredu::lunaros::internal {
void resume_debug_log_collector();
bool bind_log_capture_backend();
namespace capture {
void uninstall_debug_log_collector();
bool set_debug_log_capture(bool);
DebugLogCaptureStatus debug_log_capture_status();
DebugLogReadResult read_debug_logs(uint64_t,size_t,bool);
bool diagnostic_capture_enabled();
bool diagnostic_trace_current(DiagnosticTrace);
DiagnosticTrace begin_diagnostic_trace(const char *,uint32_t,uint64_t,DiagnosticPhase);
void record_diagnostic_trace(DiagnosticTrace,DiagnosticPhase,const char *,const char * = nullptr);
}
}
