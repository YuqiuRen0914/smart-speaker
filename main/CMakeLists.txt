set(smart_speaker_sources
    "smart_speaker/smart_speaker_audio.cpp"
    "smart_speaker/smart_speaker_amoled_list.cpp"
    "smart_speaker/smart_speaker_audio_lifecycle.cpp"
    "smart_speaker/smart_speaker_avatar.cpp"
    "smart_speaker/smart_speaker_boot.cpp"
    "smart_speaker/smart_speaker_character.cpp"
    "smart_speaker/smart_speaker_cover.cpp"
    "smart_speaker/smart_speaker_cover_api.cpp"
    "smart_speaker/smart_speaker_display.cpp"
    "smart_speaker/smart_speaker_home.cpp"
    "smart_speaker/smart_speaker_home_pet.cpp"
    "smart_speaker/smart_speaker_launcher.cpp"
    "smart_speaker/smart_speaker_dsp.cpp"
    "smart_speaker/smart_speaker_library_commands.cpp"
    "smart_speaker/smart_speaker_log_buffer.cpp"
    "smart_speaker/smart_speaker_lyrics_api.cpp"
    "smart_speaker/smart_speaker_mp3_guard.cpp"
    "smart_speaker/smart_speaker_noise.cpp"
    "smart_speaker/smart_speaker_pet_frame_api.cpp"
    "smart_speaker/smart_speaker_projection.cpp"
    "smart_speaker/smart_speaker_runtime.cpp"
    "smart_speaker/smart_speaker_sensors.cpp"
    "smart_speaker/smart_speaker_touch_routing.cpp"
    "smart_speaker/smart_speaker_transfer.cpp"
    "smart_speaker/smart_speaker_xiaozhi.cpp"
)


idf_component_register(
    SRCS
        "app_main.cpp"
        "assets/home_clock_font.c"
        "product/runtime_tasks.cpp"
        "product/service_drivers.cpp"
    "product/product_state.cpp"
    "product/product_state_runtime.cpp"
    "product/desktop_resources.cpp"
    "product/weather.cpp"
        ${smart_speaker_sources}
    INCLUDE_DIRS
        "."
        "smart_speaker"
    REQUIRES
        lunaredu_account_resources
        lunaredu_character_assets
        bblanchon__arduinojson
        chmorgan__esp-audio-player
        esp_driver_gpio
        esp_driver_i2s
        esp_app_format
        esp_timer
        nvs_flash
        espressif__esp-dsp
        lvgl__lvgl
        lunaros
        esp_http_client
        esp_http_server
        esp_wifi
        lunaredu_bsp
        lunaredu_device_gpio_button
        lunaredu_device_mpu6050
        lunaredu_device_sdcard
        lunaredu_device_spi_lcd
        lunaredu_pet
        lunaredu_audio
)


lunaros_create_storage_pool("${LUNAREDU_APP_DIR}/data" FONT)

target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/assets/home_pet_idle.bin" BINARY)

if(CONFIG_LUNAREDU_DEVICE_WEB)
    target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/web/home.html" TEXT)
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)

# Keep compute-heavy product loops optimized in a debuggable firmware build.
set_source_files_properties(smart_speaker/smart_speaker_character.cpp
    smart_speaker/smart_speaker_display.cpp
    smart_speaker/smart_speaker_dsp.cpp
    smart_speaker/smart_speaker_audio.cpp
    PROPERTIES COMPILE_OPTIONS "-O2")

# Audio has per-sample hot loops on both cores. Debug -Og makes them dominate
# display scheduling during MP3 playback; retain debug symbols with -O2.
idf_component_get_property(speaker_mp3_lib chmorgan__esp-libhelix-mp3 COMPONENT_LIB)
target_compile_options(${speaker_mp3_lib} PRIVATE -O2)
idf_component_get_property(speaker_player_lib chmorgan__esp-audio-player COMPONENT_LIB)
target_compile_options(${speaker_player_lib} PRIVATE -O2)

# Keep UI/glyph storage out of scarce internal RAM needed by TLS and DMA.
# LVGL's default 64 KiB static pool occupied DRAM and constrained both the
# expanded settings UI and cloud-avatar delivery. The board has 8 MiB PSRAM.
idf_component_get_property(speaker_lvgl_lib lvgl__lvgl COMPONENT_LIB)
idf_component_get_property(speaker_lvgl_dir lvgl__lvgl COMPONENT_DIR)
set_source_files_properties(
    "${speaker_lvgl_dir}/src/stdlib/builtin/lv_mem_core_builtin.c"
    TARGET_DIRECTORY ${speaker_lvgl_lib}
    PROPERTIES COMPILE_OPTIONS "-include;esp_attr.h"
               COMPILE_DEFINITIONS "LV_ATTRIBUTE_LARGE_RAM_ARRAY=EXT_RAM_BSS_ATTR")

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=MP3Decode" "-Wl,--undefined=__wrap_MP3Decode")

# The shared storage pool keeps Chinese glyphs outside both OTA slots.
