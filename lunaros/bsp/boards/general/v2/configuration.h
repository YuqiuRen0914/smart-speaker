#pragma once
#include "sdkconfig.h"

// Reject hand-edited configurations too: NONE and another choice symbol
// may both be defined even though Kconfig normally makes them exclusive.
#if CONFIG_LUNAROS_BSP_V2_ENABLED && \
    (!CONFIG_LUNAREDU_BSP_PROFILE_NONE || CONFIG_LUNAREDU_BSP_PROFILE_BASE || \
     CONFIG_LUNAREDU_BSP_PROFILE_CAM_BUNDLE || CONFIG_LUNAREDU_BSP_PROFILE_MINIMAL || \
     CONFIG_LUNAREDU_BSP_PROFILE_XIAOXING || CONFIG_LUNAREDU_BSP_PROFILE_REMOTE_CONTROLLER || \
     CONFIG_LUNAREDU_BSP_PROFILE_REULEAUX_TRIANGLE_V3)
#error "Experimental general-board v2 requires the NONE existing-board profile exclusively"
#endif
