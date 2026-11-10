set(device_sources
    src/disabled.cpp
)
if(CONFIG_LUNAREDU_DEVICE_ENABLE_AUDIO)
    list(APPEND device_sources
        src/i2s_pcm_audio_device.cpp
    )
endif()
if(CONFIG_LUNAREDU_DEVICE_I2S_AUDIO)
    list(APPEND device_sources
        src/device_i2s_audio_config.cpp
    )
endif()

idf_component_register(
    SRCS src/test.cpp ${device_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES lunaredu_bsp
    PRIV_REQUIRES lunaros esp_driver_gpio esp_driver_i2s
)


if(device_sources)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
endif()
