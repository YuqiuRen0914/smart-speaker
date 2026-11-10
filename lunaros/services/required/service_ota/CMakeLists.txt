set(ota_root "${CMAKE_CURRENT_LIST_DIR}")
if(CMAKE_SCRIPT_MODE_FILE)
    file(GLOB ota_sources "${ota_root}/src/*.cpp")
else()
    file(GLOB ota_sources CONFIGURE_DEPENDS "${ota_root}/src/*.cpp")
endif()

# OTA owns image installation and optional HTTPS pulling. Framework integration
# is declared by the OS internal contract headers.
idf_component_register(
    SRCS ${ota_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES device_api core bblanchon__arduinojson
    PRIV_REQUIRES service_resource service_storage configuration service_network service_download freertos log digest app_update esp_app_format esp_system bootloader_support
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
idf_build_get_property(ota_components BUILD_COMPONENTS)
if("service_storage" IN_LIST ota_components AND CONFIG_LUNAROS_NETWORK_ESPNOW_MANAGEMENT)
    idf_component_optional_requires(PRIVATE service_storage espnow)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LUNAROS_ESPNOW_OTA_STORAGE=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LUNAROS_ESPNOW_OTA_STORAGE=0)
endif()
if(CONFIG_LUNAREDU_OTA_RECOVERY_LAYOUT OR CONFIG_LUNAREDU_OTA_DUAL_SLOT_LAYOUT)
    # Archive extraction and section GC must not discard the binary contract.
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lunaros_recovery_image_contract")
endif()
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
