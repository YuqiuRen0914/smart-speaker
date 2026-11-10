set(lunaredu_bsp_sources
    "src/analog_adc_unit_pool.cpp"
    "src/uart_port.cpp"
    "src/bsp_board.cpp"
    "src/bsp_camera.cpp"
    "src/bsp_i2c.cpp"
    "src/bsp_i2s.cpp"
    "src/bsp_led.cpp"
    "src/bsp_resources.cpp"
    "src/bsp_shared_pins.cpp"
    "src/bsp_spi.cpp"
)

# ESP-IDF resolves the component graph before project Kconfig values are
# available. Keep optional board-driver components in the graph, and use the
# profile only to select BSP source files.
set(lunaredu_bsp_requires
    esp_adc
    esp_driver_uart
    esp_driver_gpio
    esp_driver_i2c
    esp_driver_spi
    esp_driver_ledc
    esp_timer
)

set(lunaredu_bsp_priv_requires
    esp_driver_sdmmc
    fatfs
    sdmmc
)

if(CONFIG_LUNAREDU_BSP_HAS_ONBOARD_TFCARD)
    list(APPEND lunaredu_bsp_sources "src/bsp_sdcard.cpp")
else()
    list(APPEND lunaredu_bsp_sources "src/bsp_sdcard_disabled.cpp")
endif()

idf_component_register(
    SRCS
        ${lunaredu_bsp_sources}
    INCLUDE_DIRS
        "include"
    REQUIRES
        ${lunaredu_bsp_requires}
    PRIV_REQUIRES
        ${lunaredu_bsp_priv_requires}
)
