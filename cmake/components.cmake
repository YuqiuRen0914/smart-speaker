# Explicit smart-speaker component closure. Preserve owner directories.
set(speaker_component_paths
    components/device_protocol
    components/digest
    components/espnow
    components/image_codec
    components/lvgl
    components/micropython
    components/network_provisioning_codec
    components/qr
    components/text_raster
    components/wamr
    components_device/display/lunaredu_device_spi_lcd
    components_device/display/lunaredu_device_spi_lcd_api
    components_device/media/lunaredu_device_sdcard
    components_device/sensors/lunaredu_device_mpu6050
    lunaros/apps/optional/app_music
    lunaros/apps/optional/app_pet
    lunaros/apps/required/app_files
    lunaros/apps/required/app_python
    lunaros/apps/required/app_settings
    lunaros/apps/required/app_store
    lunaros/assets/lunaros_asset_icons
    lunaros/assets/lunaros_asset_ui
    lunaros/bsp
    lunaros/os/application
    lunaros/os/application/adapters/device_api_application
    lunaros/os/configuration
    lunaros/os/configuration/adapters/device_api_configuration
    lunaros/os/core
    lunaros/os/desktop
    lunaros/os/device_api
    lunaros/os/display
    lunaros/os/runtime
    lunaros/services/optional/service_audio
    lunaros/services/optional/service_diagnostics
    lunaros/services/optional/service_imu
    lunaros/services/optional/service_provisioning
    lunaros/services/optional/service_python
    lunaros/services/optional/service_wasm
    lunaros/services/required/service_applications
    lunaros/services/required/service_download
    lunaros/services/required/service_factory
    lunaros/services/required/service_local_web
    lunaros/services/required/service_network
    lunaros/services/required/service_ota
    lunaros/services/required/service_platform_link
    lunaros/services/required/service_resource
    lunaros/services/required/service_storage
    lunaros/services/required/service_usb
)
foreach(relative IN LISTS speaker_component_paths)
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../${relative}/CMakeLists.txt")
        message(FATAL_ERROR "Missing bundled component: ${relative}")
    endif()
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../${relative}")
endforeach()
