#pragma once

#include <stddef.h>
#include <string>

#include "lunaredu_lunaros/kernel/service_registry.h"

namespace lunaredu::lunaros {

enum class ServiceStartDisposition {
  Ready,
  Degraded,
  Deferred,
  Failed,
};

struct ServiceStartResult {
  ServiceStartDisposition disposition = ServiceStartDisposition::Failed;
  std::string detail;
};

using ServiceStartCallback = ServiceStartResult (*)(void *context);
using ServiceStopCallback = bool (*)(void *context, std::string &error);

// A ServiceNode is the complete, explicit lifecycle contribution installed by
// a product definition. Nothing is auto-registered through global
// constructors or string-topic side effects.
struct ServiceNode {
  ServiceDefinition definition;
  ServiceStartCallback start = nullptr;
  ServiceStopCallback stop = nullptr;
  void *context = nullptr;
  // Deferred services use activate on their declared owner executor. This
  // keeps asynchronous startup inside ServiceHost instead of letting product
  // tasks call concrete service implementations directly.
  ServiceStartCallback activate = nullptr;
};

// ServiceHost storage and transition machinery are private Kernel details.
// These commands are the stable product-facing lifecycle surface.
bool start_all_services(std::string &error);
bool activate_service(const char *service_id, std::string &error);
bool report_service_ready(const char *service_id, const char *detail,
                          std::string &error);
bool report_service_degraded(const char *service_id, const char *detail,
                             std::string &error);
bool report_service_failed(const char *service_id, const char *detail,
                           std::string &error);
bool stop_all_services(std::string &error);

} // namespace lunaredu::lunaros
