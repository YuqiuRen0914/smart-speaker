# Desktop shell and system UI composition. Display owns the scheduling backend.
include(${CMAKE_CURRENT_LIST_DIR}/../../source_ownership.cmake)
set(desktop_sources ${lunar_desktop_sources} ${lunaros_composition_sources})
list(APPEND desktop_sources ${lunar_graphics_sources})
idf_component_register(
  SRCS ${desktop_sources}
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "src"
  REQUIRES application display  core  text_raster qr lunaros_asset_icons lunaros_asset_ui
  PRIV_REQUIRES configuration freertos esp_timer heap image_codec mbedtls)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
if(DEFINED LUNAROS_STATUS_HANDLE_CACHE)
  set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/src/ui/status_bar_cache.cpp" APPEND PROPERTY
    COMPILE_DEFINITIONS "LUNAROS_STATUS_HANDLE_CACHE=${LUNAROS_STATUS_HANDLE_CACHE}")
endif()
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)

# Pixel loops run every frame; retain strict floating-point semantics while
# allowing normal speed optimization even in a firmware optimized for size.
set_source_files_properties(src/shell/renderer.cpp src/navigation/home_navigation_renderer.cpp src/graphics/desktop.cpp
  src/graphics/page.cpp src/graphics/scene_icon.cpp src/graphics/system_canvas.cpp src/ui/status_bar.cpp src/ui/status_bar_cache.cpp
  PROPERTIES COMPILE_OPTIONS "-O2")
