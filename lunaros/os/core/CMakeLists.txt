include(${CMAKE_CURRENT_LIST_DIR}/../../source_ownership.cmake)
idf_component_register(
  SRCS ${lunaros_core_sources}
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "src"
  REQUIRES freertos log esp_timer heap
  PRIV_REQUIRES app_update esp_app_format
    esp_hw_support esp_system spi_flash esp_rom)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
if(DEFINED LUNAROS_REQUIRED_HEALTH_CACHE)
  set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/src/kernel/services/service_registry.cpp" APPEND PROPERTY
    COMPILE_DEFINITIONS "LUNAROS_REQUIRED_HEALTH_CACHE=${LUNAROS_REQUIRED_HEALTH_CACHE}")
endif()
idf_component_optional_requires(PRIVATE xtensa)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/src/model/device_activity_registry.cpp"
  APPEND PROPERTY COMPILE_OPTIONS "-Werror=frame-larger-than=2048")
