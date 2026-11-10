# Called by native project(), before IDF consumes bootloader component paths.
# Nested vendor projects and bootloader subprojects must not register it again.
if(NOT COMMAND idf_build_set_property OR BOOTLOADER_BUILD)
  return()
endif()
# IDF has prepared its root component list before native project() invokes this
# hook. Expand that list here, even when a product sets COMPONENTS main after
# including the repository helper. No dependency from Core/Desktop to Apps.
if(DEFINED lunaredu_builtin_apps AND CMAKE_SOURCE_DIR STREQUAL LUNAREDU_APP_DIR)
  lunaros_append_builtin_app_roots(components)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/lunaros_startup.cmake)
lunaros_prepare_startup_bootloader()
