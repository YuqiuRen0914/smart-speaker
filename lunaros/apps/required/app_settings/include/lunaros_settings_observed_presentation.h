#pragma once
#include "lunaros_settings_detail.h"
#include <cstdint>
namespace lunaredu::settings {
struct SystemObservationCursor {
  const void *source=nullptr;
  uint64_t revision=0;
};
// The cursor versions immutable network/account facts. Only use it with
// SystemObservation output; app-local edited presentations use normal copies.
void observe_system_presentation(SystemPresentation &target,SystemObservationCursor &cursor,
    const SystemPresentation &sample,const SystemObservationCursor &version);
}
