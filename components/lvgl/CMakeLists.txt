include("${CMAKE_CURRENT_LIST_DIR}/verify.cmake")
if(NOT LUNAREDU_LVGL_CONFIG)
  set(LUNAREDU_LVGL_CONFIG "${CMAKE_CURRENT_LIST_DIR}/config/lv_conf.h")
endif()
if(NOT EXISTS "${LUNAREDU_LVGL_CONFIG}")
  message(FATAL_ERROR "Missing LVGL configuration: ${LUNAREDU_LVGL_CONFIG}")
endif()
get_filename_component(ui_config_dir "${LUNAREDU_LVGL_CONFIG}" DIRECTORY)
file(GLOB_RECURSE ui_engine_sources "${vendor_directory}/src/*.c"
  "${vendor_directory}/src/*.cpp")
idf_component_register(SRCS ${ui_engine_sources}
  INCLUDE_DIRS "${vendor_directory}/include" PRIV_INCLUDE_DIRS "${ui_config_dir}")
target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_KCONFIG_IGNORE
  "LV_CONF_PATH=\"${LUNAREDU_LVGL_CONFIG}\"")

# Software rasterization is a frame-budget hotspot even in size-oriented firmware.
# Optimize only these pixel loops; do not change upstream sources or FP semantics.
foreach(ui_source IN LISTS ui_engine_sources)
  if(ui_source MATCHES "/src/draw/sw/.+\\.c$")
    set_source_files_properties("${ui_source}" PROPERTIES COMPILE_OPTIONS "-O2")
  endif()
endforeach()
