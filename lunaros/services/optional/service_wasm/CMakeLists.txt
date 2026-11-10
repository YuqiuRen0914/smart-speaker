set(session_source "src/session_unavailable.cpp")
set(session_dependencies "")
if(IDF_TARGET STREQUAL "esp32s3")
  set(session_source "src/session.cpp")
  set(session_dependencies wamr heap log esp_timer)
endif()
idf_component_register(SRCS "src/backend_registration.cpp" "src/protocol.cpp" "src/aot_header.cpp" "src/application_backend.cpp" "src/artifact_profile.cpp" "${session_source}"
  INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "src" REQUIRES application core PRIV_REQUIRES ${session_dependencies})
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
