# Explicit dependency closure for the user-confirmed 1.0.35 baseline.
set(speaker_component_paths
    components/json
    components/lunaredu_account_resources
    components/lunaredu_audio
    components/lunaredu_bsp
    components/lunaredu_character_assets
    components/lunaredu_nvs
    components_device/input/lunaredu_device_gpio_button
    components_device/media/lunaredu_device_i2s_audio
    components_device/media/lunaredu_device_sdcard
    components_device/sensors/lunaredu_device_mpu6050
    components_device/display/lunaredu_device_spi_lcd
    lunaros
    lunaros_apps/lunaredu_pet
)
foreach(relative IN LISTS speaker_component_paths)
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../${relative}/CMakeLists.txt")
        message(FATAL_ERROR "Missing bundled component: ${relative}")
    endif()
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../${relative}")
endforeach()
