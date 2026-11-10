idf_component_register(
  SRCS "src/desktop_application.cpp" "src/manager_view.cpp" "src/manager_model.cpp" "src/application.cpp" "src/console.cpp" "src/app_store.cpp" "src/desktop_host.cpp" "src/runtime_console.cpp" "src/package_projection.cpp"
  INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "src"
  REQUIRES application core display desktop device_api service_network service_applications PRIV_REQUIRES service_storage bblanchon__arduinojson)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
