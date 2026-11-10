#include "diagnostics/function_probe.h"
#pragma once
#include "diagnostics/elapsed_work_timing.h"
namespace lunaredu::desktop {
// The existing maintenance/input executor records and reads this observation.
// CPU monitoring gates collection; elapsed durations include preemption and
// waiting, and nested stages must not be added to enclosing loop durations.
enum class InputWorkTimingStage : size_t { Begin,Safety,Navigation,Shell,Pointer,Application,SessionHooks,Snapshot,Policy,Controls,Destination,Command,Widgets,WidgetSelection,ShellObserve,ModelObservation,Synchronize,Persistence,Observers,Redraw,OtaSnapshot,Count };
inline constexpr std::array<const char *,size_t(InputWorkTimingStage::Count)> input_work_stage_names={"begin","safety","navigation","shell","pointer","application","session_hooks","snapshot","policy","controls","destination","command","widgets","widget_selection","shell_observe","model_observation","synchronize","persistence","observers","redraw","ota_snapshot"};
using InputWorkTiming=system::ElapsedWorkTiming<size_t(InputWorkTimingStage::Count)>;
InputWorkTiming::Snapshot read_input_work_timing();
class InputWorkScope {
 public:
  explicit InputWorkScope(InputWorkTimingStage);
  ~InputWorkScope();
  InputWorkScope(const InputWorkScope &)=delete;
 private:
  // Observation fields are inactive in host builds without ESP diagnostics.
  [[maybe_unused]] InputWorkTimingStage stage_;
  bool active_=false;
  [[maybe_unused]] uint64_t start_=0;
  [[maybe_unused]] uint32_t task_=0;
#if defined(ESP_PLATFORM) && defined(LUNAROS_DIAGNOSTIC_PROBES)
  system::probe::Scope function_scope_;
#endif
};
}
