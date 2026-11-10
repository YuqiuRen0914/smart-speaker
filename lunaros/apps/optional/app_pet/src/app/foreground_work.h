#include "diagnostics/function_probe.h"
#pragma once
#include <cstddef>
#include <utility>
#ifdef ESP_PLATFORM
#include "diagnostics/elapsed_work_report.h"
#include "diagnostics/cpu_monitor.h"
#include <esp_timer.h>
#endif

namespace lunaredu::pet::foreground_work {
enum class Stage : size_t {
  Preferences, Pairing, Connections, Decoration, Admission, Overrides, Projection,
  VisualSnapshot, PlatformEnvironment, Policy, SceneAcceptance, SceneSnapshot, Publication, Count
};
#ifdef ESP_PLATFORM
extern system::ElapsedWorkTiming<size_t(Stage::Count)> timing;
void begin();
#else
inline void begin() {}
#endif
template<class Action> decltype(auto) measure(Stage stage, Action &&action) {
  system::probe::Scope diagnostic_scope(static_cast<system::probe::Point>(unsigned(system::probe::Point::PetPreferences)+unsigned(stage)));
#ifdef ESP_PLATFORM
  return timing.measure_profiled(size_t(stage), [] { return uint64_t(esp_timer_get_time()); },
                        [] { return system::settled_current_task_runtime_us(); },
                        std::forward<Action>(action));
#else
  (void)stage;
  return std::forward<Action>(action)();
#endif
}
}
