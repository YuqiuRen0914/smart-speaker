if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT CMAKE_SCRIPT_MODE_FILE)
  get_filename_component(espnow_bundle "${CMAKE_CURRENT_LIST_DIR}/../../../../../../../components/espnow" ABSOLUTE)
  # Checked-in generated ownership port keeps normal ESP-IDF builds free of
  # Node. Preserve the original generator and verify every input/output hash.
  idf_build_get_property(espnow_python PYTHON)
  idf_build_get_property(espnow_project PROJECT_DIR)
  set(espnow_port "${CMAKE_CURRENT_LIST_DIR}/generated")
  execute_process(COMMAND "${espnow_python}" "${espnow_project}/tools/check-generated.py"
      "${CMAKE_CURRENT_LIST_DIR}/generated.json" "${espnow_project}"
      COMMAND_ERROR_IS_FATAL ANY)
  set(official_espnow_sources "${espnow_port}/espnow.c" "${espnow_port}/espnow_group.c" "${espnow_port}/espnow_security.c" "${espnow_port}/client_security1.c")
  target_sources(${COMPONENT_LIB} PRIVATE ${official_espnow_sources} "${CMAKE_CURRENT_LIST_DIR}/storage-disabled.c")
  target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/include")
  target_compile_definitions(${COMPONENT_LIB} PRIVATE ESP_NOW_VER_MAJOR=2 ESP_NOW_VER_MINOR=5 ESP_NOW_VER_PATCH=3)
  set_source_files_properties(${official_espnow_sources} "${CMAKE_CURRENT_LIST_DIR}/storage-disabled.c" PROPERTIES COMPILE_OPTIONS "-Wno-pointer-sign;-Wno-format;-Wno-unused-function;-Wno-unused-variable;-Wno-unused-parameter")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${espnow_bundle}/UPSTREAM.json" "${CMAKE_CURRENT_LIST_DIR}/materialize-port.mjs" "${CMAKE_CURRENT_LIST_DIR}/owner-poll.inc" "${CMAKE_CURRENT_LIST_DIR}/owner-lifecycle.inc")
endif()
