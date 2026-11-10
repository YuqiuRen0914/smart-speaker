set(smart_speaker_sources
 "smart_speaker/smart_speaker_audio.cpp"
 "smart_speaker/smart_speaker_library_commands.cpp"
 "smart_speaker/smart_speaker_lyrics_api.cpp"
 "smart_speaker/smart_speaker_projection.cpp"
 "smart_speaker/smart_speaker_runtime.cpp"
 "smart_speaker/smart_speaker_sensors.cpp"
 "smart_speaker/smart_speaker_transfer.cpp"
)

include("${CMAKE_CURRENT_LIST_DIR}/../product.cmake")
set(product_components ${LUNAROS_PRODUCT_COMPONENTS})
idf_component_register(
 SRCS
        "app_main.cpp"
        "product/runtime_tasks.cpp"
        "product/service_drivers.cpp"
        "product/speaker_desktop.cpp"
        ${smart_speaker_sources}
 PRIV_INCLUDE_DIRS
        "."
        "smart_speaker"
 REQUIRES ${product_components}
         service_network
        bblanchon__arduinojson
        esp_driver_gpio
        esp_app_format
        esp_timer


 PRIV_REQUIRES configuration device_api service_resource runtime service_ota service_platform_link service_storage service_local_web
)

lunaros_create_storage_pool("${LUNAREDU_APP_DIR}/data" FONT)

if(CONFIG_LUNAREDU_DEVICE_WEB)
 target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/web/home.html" TEXT)
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)


# The shared storage pool keeps Chinese glyphs outside both OTA slots.

# Direct source consumers own this optional feature dependency.
idf_component_optional_requires(PRIVATE service_provisioning)
