set(device_sources
    src/disabled.cpp
)
set(lcd_pixel_requires)
if(CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD)
    list(APPEND lcd_pixel_requires lvgl)
    list(APPEND device_sources
        src/ili9488_panel.cpp
        src/device_spi_lcd.cpp
        src/device_spi_lcd_surface.cpp
        src/rgb565_transfer.cpp
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
    SRCS src/example.cpp ${device_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_lcd
    PRIV_REQUIRES freertos core esp_driver_gpio esp_driver_spi esp_timer bsp heap ${lcd_pixel_requires}
)

# IDF expands requirements before sdkconfig is available. Bind the selected
# pure-pixel library again after configuration so its public headers propagate.
if(CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD)
    idf_component_optional_requires(PRIVATE lvgl)
endif()

if(device_sources)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
endif()

# The bounded DMA staging copy is a per-frame pixel loop, not cold driver glue.
set_source_files_properties(src/rgb565_transfer.cpp PROPERTIES COMPILE_OPTIONS "-O2")
