#pragma once
#include "kernel/service.h"
#include <string>
namespace lunaredu::lunaros { inline constexpr const char *kDiagnosticsServiceId="runtime.diagnostics"; ServiceNode diagnostics_service_node();
// Optional product policy: retain boot logs in the normal service-owned ring.
// Normal service startup reuses it; service stop releases it.
bool start_diagnostics_boot_capture(std::string &error); }
