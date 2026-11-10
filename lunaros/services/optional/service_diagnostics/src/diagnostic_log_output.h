#pragma once
#include <cstdarg>
namespace lunaredu::lunaros::internal::capture {
using DiagnosticLogOutput=void (*)(const char *,va_list);
void resume_debug_log_collector();
void attach_diagnostic_log_output(DiagnosticLogOutput);
void detach_diagnostic_log_output(DiagnosticLogOutput);
}
