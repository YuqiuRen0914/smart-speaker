set(device_sources
    src/disabled.cpp
    src/desktop_surface.cpp
)
if(CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD)
    list(APPEND device_sources
        src/ili9488_panel.cpp
        src/device_spi_lcd.cpp
        src/device_spi_lcd_surface.cpp
        src/spi_lcd_surface_local_api.cpp
    )
endif()
if(CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD)
if(CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_3WIRE_9BIT)
    list(APPEND device_sources
        src/st7789_3wire_panel.cpp
    )
endif()
endif()

idf_component_register(
    SRCS src/test.cpp ${device_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_lcd lunaros
    PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_timer lunaredu_bsp bblanchon__arduinojson
)

if(device_sources)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
endif()
