include(${CMAKE_CURRENT_LIST_DIR}/../../source_ownership.cmake)
idf_component_register(SRCS ${lunaros_device_api_sources}
  INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "src"
  REQUIRES core device_protocol bblanchon__arduinojson
  PRIV_REQUIRES esp_timer heap esp_app_format)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
if(CONFIG_COMPILER_OPTIMIZATION_PERF)
  foreach(source IN LISTS lunaros_device_api_sources)
    set_property(SOURCE ${source} APPEND PROPERTY COMPILE_OPTIONS "-Os")
  endforeach()
endif()
