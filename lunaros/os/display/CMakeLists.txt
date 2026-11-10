# Resident display scheduling; no Desktop or concrete product dependencies.
include(${CMAKE_CURRENT_LIST_DIR}/../../source_ownership.cmake)
set(display_sources ${lunaros_display_sources} ${lunaros_display_graphics_sources})
list(FILTER display_sources EXCLUDE REGEX "/src/adapters/")
idf_component_register(SRCS ${display_sources}
  INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "src" "src/graphics/backend_lvgl/config"
  REQUIRES core freertos text_raster lunaros_asset_icons
  PRIV_REQUIRES esp_timer heap lvgl lunaros_asset_ui esp_mm esp_hw_support esp_system)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
if(DEFINED LUNAROS_CANVAS_MASK_CACHE)
  set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/src/graphics/backend_lvgl/canvas_painter.cpp" APPEND PROPERTY
    COMPILE_DEFINITIONS "LUNAROS_CANVAS_MASK_CACHE=${LUNAROS_CANVAS_MASK_CACHE}")
endif()
idf_component_optional_requires(PRIVATE esp_driver_dma esp_driver_ppa lunaredu_device_spi_lcd lunaredu_device_co5300)
idf_build_get_property(display_build_components BUILD_COMPONENTS)
# Product selection activates adapters; public headers forward-declare drivers.
if("lunaredu_device_spi_lcd" IN_LIST display_build_components)
  target_sources(${COMPONENT_LIB} PRIVATE src/adapters/spi_lcd.cpp)
endif()
if("lunaredu_device_co5300" IN_LIST display_build_components)
  target_sources(${COMPONENT_LIB} PRIVATE src/adapters/co5300.cpp)
endif()
if("esp_driver_dma" IN_LIST display_build_components)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE LUNAROS_DISPLAY_DMA2D_AVAILABLE=1)
endif()
if("esp_driver_ppa" IN_LIST display_build_components)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE LUNAROS_DISPLAY_PPA_AVAILABLE=1)
endif()
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)

set_source_files_properties(src/graphics/backend_lvgl/scene.cpp src/graphics/backend_lvgl/canvas_painter.cpp src/graphics/backend_lvgl/font.cpp
    PROPERTIES COMPILE_OPTIONS "-O2")

# Preserve the existing speed choices for shared pixel loops.
set_source_files_properties(src/graphics/raster/mask.cpp src/pipeline/screen_damage.cpp src/pipeline/screen_frame_presenter.cpp PROPERTIES COMPILE_OPTIONS "-O2")
