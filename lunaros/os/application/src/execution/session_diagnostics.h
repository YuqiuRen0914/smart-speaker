#pragma once
#include "app/execution/application_session.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
#include "diagnostics/trace.h"
#endif

namespace lunaredu::lunaros::apps {
// Called by one VM owner. Parent links are explicit, never inferred from time.
class SessionDiagnostics {
public:
  static void observe(void *context, const SessionObservation &event) {
    static_cast<SessionDiagnostics *>(context)->record(event);
  }
private:
  void record([[maybe_unused]] const SessionObservation &event) {
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
    if (!diagnostic_capture_enabled()) return;
    if (event.phase == SessionObservationPhase::Opening) { root_ = {}; call_ = {}; }
    if (!diagnostic_trace_current(root_)) {
      root_ = begin_diagnostic_trace("wasm.session", 0, 0, DiagnosticPhase::Observed);
      call_ = {};
    }
    auto trace = root_;
    DiagnosticPhase phase = DiagnosticPhase::Observed;
    switch (event.phase) {
      case SessionObservationPhase::Opening: phase = DiagnosticPhase::Started; break;
      case SessionObservationPhase::Opened: phase = DiagnosticPhase::RuntimeOpened; break;
      case SessionObservationPhase::OpenFailed: phase = DiagnosticPhase::RuntimeFailed; break;
      case SessionObservationPhase::CallStarted:
        call_ = begin_diagnostic_trace(event.operation, 0, root_.id, DiagnosticPhase::Observed);
        trace = call_; phase = DiagnosticPhase::Started; break;
      case SessionObservationPhase::CallReturned:
        trace = diagnostic_trace_current(call_) ? call_ : root_;
        phase = DiagnosticPhase::Returned; break;
      case SessionObservationPhase::CallFailed:
        trace = diagnostic_trace_current(call_) ? call_ : root_;
        phase = DiagnosticPhase::RuntimeFailed; break;
      case SessionObservationPhase::HostStarted: phase = DiagnosticPhase::HostStarted; break;
      case SessionObservationPhase::HostReturned: phase = DiagnosticPhase::HostReturned; break;
      case SessionObservationPhase::HostFailed: phase = DiagnosticPhase::HostFailed; break;
      case SessionObservationPhase::Closing: phase = DiagnosticPhase::Started; break;
      case SessionObservationPhase::Closed: phase = DiagnosticPhase::RuntimeClosed; break;
    }
    if (event.phase == SessionObservationPhase::HostStarted ||
        event.phase == SessionObservationPhase::HostReturned || event.phase == SessionObservationPhase::HostFailed)
      trace = diagnostic_trace_current(call_) ? call_ : root_;
    record_diagnostic_trace(trace, phase, event.operation, event.code);
    if (event.phase == SessionObservationPhase::CallReturned || event.phase == SessionObservationPhase::CallFailed) call_ = {};
    if (event.phase == SessionObservationPhase::Closed) { root_ = {}; call_ = {}; }
#endif
  }
#if CONFIG_LUNAREDU_SYSTEM_DIAGNOSTICS || CONFIG_LUNAREDU_LOCAL_DEBUG_SURFACE
  DiagnosticTrace root_{}, call_{};
#endif
};
}
