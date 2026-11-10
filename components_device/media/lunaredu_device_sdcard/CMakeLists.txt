set(device_sources
    src/disabled.cpp
)
if(CONFIG_LUNAREDU_DEVICE_ENABLE_TFCARD)
    list(APPEND device_sources
        src/tfcard_store.cpp
    )
endif()
if(CONFIG_LUNAREDU_DEVICE_STORAGE_SDCARD)
    list(APPEND device_sources
        src/device_sdcard_storage.cpp
    )
endif()

idf_component_register(
    SRCS src/example.cpp src/native_session.cpp ${device_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES device_api fatfs sdmmc esp_driver_sdmmc esp_driver_gpio freertos esp_timer vfs bsp core
)

if(device_sources)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
endif()

