include(${CMAKE_CURRENT_LIST_DIR}/../../source_ownership.cmake)
idf_component_register(SRCS ${lunaros_application_sources}
  INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "src"
  REQUIRES core bblanchon__arduinojson
  PRIV_REQUIRES digest esp_timer heap)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
if(CONFIG_COMPILER_OPTIMIZATION_PERF)
  foreach(source IN LISTS lunaros_application_sources)
    if(source MATCHES "/src/codec/")
      set_property(SOURCE ${source} APPEND PROPERTY COMPILE_OPTIONS "-Os")
    endif()
  endforeach()
endif()
