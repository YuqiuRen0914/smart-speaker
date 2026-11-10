# OS-owned hardware foundation; independent of the OS core, services and Device.
set(lunaredu_bsp_sources
    "boards/bsp_board.cpp"
    "boards/system_facts.cpp"
    "boards/selection.cpp"
    "boards/board_descriptor.cpp"
    "resources/analog_adc_unit_pool.cpp"
    "resources/uart_port.cpp"
    "resources/bsp_resources.cpp"
    "resources/bsp_i2s.cpp"
    "resources/bsp_shared_pins.cpp"
    "buses/bsp_i2c.cpp"
    "buses/bsp_spi.cpp"
    "legacy/expansion_baseboard/resources.cpp"
    "legacy/expansion_baseboard/bsp_camera.cpp"
    "legacy/expansion_baseboard/bsp_led.cpp"
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
    esp_hal_gpio
    esp_rom
    soc
    esp_driver_sdmmc
    fatfs
    sdmmc
    esp_driver_sdspi
    pthread
)

# The candidate general-board revision stays opt-in. Its disabled contract
# has no bus handles, resource owners or mutexes in existing product builds.
if(CONFIG_LUNAROS_BSP_V2_ENABLED)
    if(NOT CONFIG_LUNAREDU_BSP_PROFILE_NONE OR CONFIG_LUNAREDU_BSP_PROFILE_BASE
        OR CONFIG_LUNAREDU_BSP_PROFILE_CAM_BUNDLE OR CONFIG_LUNAREDU_BSP_PROFILE_MINIMAL
        OR CONFIG_LUNAREDU_BSP_PROFILE_XIAOXING OR CONFIG_LUNAREDU_BSP_PROFILE_REMOTE_CONTROLLER
        OR CONFIG_LUNAREDU_BSP_PROFILE_REULEAUX_TRIANGLE_V3)
        message(FATAL_ERROR "Experimental general-board v2 cannot share a build with an existing board profile")
    endif()
    list(APPEND lunaredu_bsp_sources
        "boards/general/v2/board.cpp"
        "boards/general/v2/resources/resources.cpp"
        "boards/general/v2/resources/audio.cpp"
        "boards/general/v2/buses/i2c.cpp"
        "boards/general/v2/buses/spi.cpp"
        "boards/general/v2/buses/sdcard.cpp"
    )
else()
    list(APPEND lunaredu_bsp_sources "boards/general/v2/disabled.cpp")
endif()

if(CONFIG_LUNAREDU_BSP_HAS_ONBOARD_TFCARD)
    list(APPEND lunaredu_bsp_sources "legacy/expansion_baseboard/bsp_sdcard.cpp")
else()
    list(APPEND lunaredu_bsp_sources "buses/bsp_sdcard_disabled.cpp")
endif()

idf_component_register(
    SRCS
        ${lunaredu_bsp_sources}
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "."
    REQUIRES
        ${lunaredu_bsp_requires}
    PRIV_REQUIRES
        ${lunaredu_bsp_priv_requires}
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include(${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/lunaros_startup.cmake)
    lunaros_configure_startup(${COMPONENT_LIB})
endif()
