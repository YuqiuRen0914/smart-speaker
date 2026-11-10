# Standalone engine selection. LunarOS discovers the supported engine itself
# as part of its built-in application management dependency closure.
# Replace stale cache overrides with the repository-owned source.
set(LUNAREDU_WAMR_SOURCE "${LUNAREDU_REPO_ROOT}/components/wamr/upstream"
  CACHE PATH "Repository-owned WAMR source" FORCE)
include("${CMAKE_CURRENT_LIST_DIR}/verify.cmake")
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}")
