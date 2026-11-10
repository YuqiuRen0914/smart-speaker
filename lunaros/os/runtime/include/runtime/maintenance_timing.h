#pragma once
#include "diagnostics/elapsed_work_timing.h"
namespace lunaredu::lunaros {
// The existing maintenance/input executor records and reads this observation.
// CPU monitoring gates collection; elapsed durations include preemption and
// waiting, and nested stages must not be added to enclosing loop durations.
enum class MaintenanceWorkTimingStage : size_t { MainExecutor,NetworkExecutor,Lifecycle,Network,Web,Product,PlatformBoundary,Count };
inline constexpr std::array<const char *,size_t(MaintenanceWorkTimingStage::Count)> maintenance_work_stage_names={"main_executor","network_executor","lifecycle","network","web","product","platform_tick_boundary"};
using MaintenanceWorkTiming=system::ElapsedWorkTiming<size_t(MaintenanceWorkTimingStage::Count)>;
MaintenanceWorkTiming::Snapshot read_maintenance_work_timing();
}
