idf_component_register(SRCS "src/desktop_session.cpp" "src/view.cpp" "src/input_takeover.cpp" "src/application.cpp" "src/device_api.cpp" "src/device_handlers.cpp" "src/control_dispatch.cpp" "src/project_packages.cpp" "src/script_catalog.cpp" INCLUDE_DIRS "include"
  REQUIRES device_api application desktop core service_python
  PRIV_REQUIRES device_api_application service_storage bblanchon__arduinojson esp_timer)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)

# This is project/catalog management, not the MicroPython execution engine.
if(CONFIG_COMPILER_OPTIMIZATION_PERF)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Os)
endif()
