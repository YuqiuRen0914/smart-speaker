#pragma once
#include "application_maintenance_state.h"

namespace lunaredu::lunaros::apps::runtime_detail {
struct RetiredApplicationOwner {
  RetiredApplicationOwner *next = nullptr;
  void *context = nullptr;
  KernelRuntime *runtime = nullptr;
  bool (*stop)(void *) = nullptr;
  void (*destroy)(void *) = nullptr;
};
void adopt_retired_application(RetiredApplicationOwner &);
bool poll_retired_applications();
bool has_retired_applications(KernelRuntime *);
}
