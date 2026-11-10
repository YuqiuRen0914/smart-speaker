# Product opt-in; source is repository-owned and checked offline.
include("${CMAKE_CURRENT_LIST_DIR}/verify.cmake")
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}")
list(APPEND COMPONENTS micropython)
