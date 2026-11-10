#include "bsp/lunaredu_bsp.h"
#include "sdkconfig.h"

#include "boards/general/v2/configuration.h"

#if !CONFIG_LUNAROS_BSP_ENABLE_LEGACY_BASEBOARD && \
    (CONFIG_LUNAREDU_BSP_PROFILE_BASE || \
     CONFIG_LUNAREDU_BSP_PROFILE_CAM_BUNDLE || \
     CONFIG_LUNAREDU_BSP_PROFILE_MINIMAL)
#error "Historical board profiles require explicit LUNAROS_BSP_ENABLE_LEGACY_BASEBOARD"
#endif

bsp_board_profile_t bsp_board_profile() {
#if CONFIG_LUNAREDU_BSP_PROFILE_REULEAUX_TRIANGLE_V3
  return BSP_BOARD_PROFILE_REULEAUX_TRIANGLE_V3;
#elif CONFIG_LUNAREDU_BSP_PROFILE_XIAOXING
  return BSP_BOARD_PROFILE_XIAOXING;
#elif CONFIG_LUNAREDU_BSP_PROFILE_REMOTE_CONTROLLER
  return BSP_BOARD_PROFILE_REMOTE_CONTROLLER;
#elif CONFIG_LUNAREDU_BSP_PROFILE_MINIMAL
  return BSP_BOARD_PROFILE_MINIMAL;
#elif CONFIG_LUNAREDU_BSP_PROFILE_BASE
  return BSP_BOARD_PROFILE_BASE;
#elif CONFIG_LUNAREDU_BSP_PROFILE_CAM_BUNDLE
  return BSP_BOARD_PROFILE_CAM_BUNDLE;
#else
  return BSP_BOARD_PROFILE_NONE;
#endif
}

const char *bsp_board_profile_name() {
  switch (bsp_board_profile()) {
  case BSP_BOARD_PROFILE_NONE:
    return "none";
  case BSP_BOARD_PROFILE_BASE:
    return "base";
  case BSP_BOARD_PROFILE_CAM_BUNDLE:
    return "cam_bundle";
  case BSP_BOARD_PROFILE_MINIMAL:
    return "minimal";
  case BSP_BOARD_PROFILE_XIAOXING:
    return "xiaoxing";
  case BSP_BOARD_PROFILE_REMOTE_CONTROLLER:
    return "remote_controller";
  case BSP_BOARD_PROFILE_REULEAUX_TRIANGLE_V3:
    return "reuleaux_triangle_v3";
  default:
    return "unknown";
  }
}

