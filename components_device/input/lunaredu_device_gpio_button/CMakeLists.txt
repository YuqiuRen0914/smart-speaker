set(device_sources
    src/polled_gpio_button.cpp
    src/disabled.cpp
)
if(CONFIG_LUNAREDU_DEVICE_INPUT_PRIMARY_BUTTON OR CONFIG_LUNAREDU_DEVICE_INPUT_PTT_BUTTON)
    list(APPEND device_sources
        src/device_gpio_button.cpp
    )
endif()

idf_component_register(
    SRCS src/test.cpp ${device_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES lunaros esp_driver_gpio
)

if(CONFIG_LUNAREDU_DEVICE_INPUT_PRIMARY_BUTTON)
    idf_component_optional_requires(PRIVATE espressif__button)
endif()

if(CONFIG_LUNAREDU_DEVICE_INPUT_PTT_BUTTON)
    idf_component_optional_requires(PRIVATE espressif__button)
endif()

if(device_sources)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
endif()
