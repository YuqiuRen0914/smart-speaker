# Select the pinned upstream engine for a Display consumer.
# FORCE replaces an old build directory's .build/deps cache setting.
set(LUNAROS_LVGL_SOURCE "${LUNAREDU_REPO_ROOT}/components/lvgl/upstream"
  CACHE PATH "Repository-owned LVGL source" FORCE)
include("${CMAKE_CURRENT_LIST_DIR}/verify.cmake")
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}")
# IDF extracts requirements before sdkconfig symbols are available. Selecting
# the engine here makes Display's fixed private dependency discoverable.
list(APPEND COMPONENTS lvgl)
