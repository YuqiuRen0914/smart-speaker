idf_component_register(
  SRCS "src/app/application_foreground_ports.cpp" "src/app/application_maintenance_ports.cpp" "src/app/application_admission.cpp"
       "src/app/application_catalog.cpp"
       "src/app/application_installation.cpp"
       "src/app/application_maintenance_io.cpp"
       "src/app/application_maintenance_state.cpp"
       "src/app/applications_service.cpp"
       "src/app/application_saves.cpp"
       "src/persistence/application_records_json.cpp"
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "src"
  REQUIRES application core
  PRIV_REQUIRES service_storage bblanchon__arduinojson freertos log esp_timer
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)

if(CONFIG_COMPILER_OPTIMIZATION_PERF)
  # Catalog/package/save transactions run on management paths. Keep admission,
  # foreground/session lifecycle and maintenance scheduling at their defaults.
  set_source_files_properties(
    src/app/application_catalog.cpp
    src/app/application_saves.cpp
    src/app/application_installation.cpp
    src/persistence/application_records_json.cpp
    PROPERTIES COMPILE_OPTIONS "-Os")
endif()
