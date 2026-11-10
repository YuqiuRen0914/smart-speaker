#include "app/foreground_work.h"
#ifdef ESP_PLATFORM
#if CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#include <esp_attr.h>
#endif
namespace lunaredu::pet::foreground_work {
#if CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
EXT_RAM_BSS_ATTR
#endif
system::ElapsedWorkTiming<size_t(Stage::Count)> timing;
namespace {
system::ElapsedWorkReporter<size_t(Stage::Count)> reporter;
}
void begin() {
  timing.enable(system::main_work_profiling_active());
  reporter.report("pet.foreground", timing.snapshot(),
      {"preferences", "pairing", "connections", "decoration", "admission", "overrides", "projection",
       "visual_snapshot", "platform_environment", "policy", "scene_acceptance", "scene_snapshot", "publication"});
}
}
#endif
