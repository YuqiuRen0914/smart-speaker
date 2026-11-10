#pragma once
#include "display/pipeline/pipeline_metrics.h"
#include "display/pipeline/frame_cadence.h"

namespace lunaredu::display {
enum class ScreenLifecycle { Unconfigured, Prepared, Running, Failed, FailedResident, Stopped };
inline constexpr const char *screen_lifecycle_name(ScreenLifecycle state) {
  switch(state) {
    case ScreenLifecycle::Stopped:return "stopped";
    case ScreenLifecycle::Unconfigured:return "unconfigured";
    case ScreenLifecycle::Prepared:return "prepared";
    case ScreenLifecycle::Running:return "running";
    case ScreenLifecycle::Failed:return "failed";
    case ScreenLifecycle::FailedResident:return "failed_resident";
  }
  return "unknown";
}
struct ScreenStorageSnapshot {
  ScreenLifecycle lifecycle=ScreenLifecycle::Unconfigured;
  uint32_t frame_count=0;
  uint64_t frame_bytes=0, mailbox_bytes=0, retained_bytes=0;
  uint64_t budget_bytes=0,reserved_bytes=0;
};
struct ScreenTransferSnapshot {
  uint64_t pending_request=0;
  uint32_t control_timeouts=0, late_replies=0, mismatched_replies=0, frame_timeouts=0;
};
// Safe to sample across owners: individually atomic observations, not a
// transaction. Storage accounts for frames/mailbox, excluding queues/stacks.
struct ScreenSessionSnapshot : ScreenPipelineSnapshot {
  ScreenStorageSnapshot storage;
  ScreenTransferSnapshot control;
  uint32_t target_fps=kScreenTargetFps,coalesced_jobs=0;
  uint32_t render_budget_misses=0,transfer_budget_misses=0;
  uint64_t pacing_wait_us=0;
};
}
