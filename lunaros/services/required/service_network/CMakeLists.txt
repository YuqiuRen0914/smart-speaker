if(CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE)
  # ESP-IDF's bundle generator silently ignores other filename extensions.
  if(COMMAND idf_build_get_property)
    idf_build_get_property(custom_ca_project_dir PROJECT_DIR)
  else()
    set(custom_ca_project_dir "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  get_filename_component(custom_ca_path "${CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH}" ABSOLUTE BASE_DIR "${custom_ca_project_dir}")
  get_filename_component(custom_ca_extension "${custom_ca_path}" EXT)
  if(NOT IS_DIRECTORY "${custom_ca_path}" AND NOT custom_ca_extension STREQUAL ".pem" AND NOT custom_ca_extension STREQUAL ".der")
    message(FATAL_ERROR "Custom TLS CA bundle must use a .pem or .der filename; ESP-IDF ignores other extensions")
  endif()
endif()

if(CMAKE_SCRIPT_MODE_FILE)
  file(GLOB_RECURSE network_sources "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp")
else()
  file(GLOB_RECURSE network_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp")
endif()
idf_component_register(SRCS ${network_sources} INCLUDE_DIRS include PRIV_INCLUDE_DIRS src REQUIRES device_api device_protocol core PRIV_REQUIRES device_api_configuration configuration esp_event esp_netif esp_wifi espnow esp_http_client tcp_transport esp_timer esp-tls esp_system bt lwip mbedtls  heap bsp protocomm protobuf-c esp_pm app_update esp_partition esp_app_format)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
include("${CMAKE_CURRENT_LIST_DIR}/src/ports/espnow/sources.cmake")
if(CONFIG_LUNAROS_NETWORK_ESPNOW_MANAGEMENT)
  idf_component_optional_requires(PRIVATE protocomm)
endif()
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)

if(CONFIG_COMPILER_OPTIMIZATION_PERF)
  # Configuration and projections are cold paths. Packet transport, Wi-Fi,
  # Bluetooth lifecycle and the SDK radio/crypto code retain their settings.
  set_source_files_properties(
    src/projections/network_projection.cpp
    src/projections/network_projection_handlers.cpp
    src/projections/wifi_connection_projection.cpp
    src/projections/wifi_config_payload.cpp
    src/network/wifi/wifi_configuration.cpp
    src/network/http/tls_policy.cpp
    PROPERTIES COMPILE_OPTIONS "-Os")
endif()

if(NOT CMAKE_SCRIPT_MODE_FILE)
  include("${CMAKE_CURRENT_LIST_DIR}/../../../../tools/cmake/esp-tls-connect-poll.cmake")
  # SDK component targets are all registered by the time the root finishes.
  cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL lunaros_fix_esp_tls_connect_poll)
  include("${CMAKE_CURRENT_LIST_DIR}/../../../../tools/cmake/esp-tls-session-poll.cmake")
  cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL lunaros_fix_esp_tls_session_poll)
endif()

# Protocol profiles share Network lifecycle. Audio is compiled only when an
# audio-capable product selects that owner and the SDK has classic music.
idf_build_get_property(network_components BUILD_COMPONENTS)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  LUNAROS_NETWORK_BLUETOOTH_PROFILES=$<BOOL:${CONFIG_LUNAROS_NETWORK_BLUETOOTH_PROFILES}>)
if("service_audio" IN_LIST network_components AND CONFIG_BT_CLASSIC_ENABLED
    AND CONFIG_BT_A2DP_ENABLE AND CONFIG_BT_AVRCP_ENABLED)
  idf_component_optional_requires(PRIVATE service_audio)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE LUNAROS_NETWORK_BLUETOOTH_AUDIO=1)
else()
  target_compile_definitions(${COMPONENT_LIB} PRIVATE LUNAROS_NETWORK_BLUETOOTH_AUDIO=0)
endif()
