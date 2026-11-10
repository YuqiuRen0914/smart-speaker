file(GLOB_RECURSE lunaros_micropython_sources
    "${CMAKE_CURRENT_LIST_DIR}/src/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp")

if(NOT CONFIG_LUNAREDU_MICROPYTHON_ENABLE_HARDWARE_BINDINGS)
    list(FILTER lunaros_micropython_sources EXCLUDE REGEX ".*/src/native/lunaredu_hardware/micropython_lunaredu_module\\.cpp$")
endif()

set(lunaros_micropython_priv_requires
    bblanchon__arduinojson
    esp_timer
)

if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_HARDWARE_BINDINGS)
    # Read-only storage telemetry is a language adapter; script policy is App-owned.
    list(APPEND lunaros_micropython_priv_requires service_storage)
    if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_ASRPRO_BINDING)
        list(APPEND lunaros_micropython_priv_requires lunaredu_device_asrpro)
    endif()
    if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_CAMERA_BINDING)
        list(APPEND lunaros_micropython_priv_requires lunaredu_device_camera)
    endif()
    if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_TFCARD_BINDING)
        list(APPEND lunaros_micropython_priv_requires lunaredu_device_sdcard)
    endif()
    if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_SERVO_BINDING)
        list(APPEND lunaros_micropython_priv_requires lunaredu_device_servo)
    endif()
    if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_SCREEN_BINDING)
        list(APPEND lunaros_micropython_priv_requires lunaredu_device_spi_lcd)
    endif()
    if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_RGB_BINDING)
        list(APPEND lunaros_micropython_priv_requires lunaredu_device_ws2812)
    endif()
    if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_IMU_BINDING)
        list(APPEND lunaros_micropython_priv_requires
            lunaredu_device_mpu6050
        )
    endif()
endif()

if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_VISION_BINDINGS)
    list(APPEND lunaros_micropython_priv_requires
        service_vision
        image_codec
    )
endif()

if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_LUNARLINK_BINDINGS)
    list(APPEND lunaros_micropython_priv_requires service_assistant)
endif()

idf_component_register(
    SRCS ${lunaros_micropython_sources}
    INCLUDE_DIRS
        "include"
    REQUIRES application core
    PRIV_INCLUDE_DIRS
        "src"
    PRIV_REQUIRES device_api_application device_api
        micropython
        ${lunaros_micropython_priv_requires}
)

if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_HARDWARE_BINDINGS)
    # lunaredu.filesystem exposes the public storage status facade.
    idf_component_optional_requires(PRIVATE service_storage)
    if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_ASRPRO_BINDING)
        idf_component_optional_requires(PRIVATE lunaredu_device_asrpro)
    endif()
    if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_CAMERA_BINDING)
        idf_component_optional_requires(PRIVATE lunaredu_device_camera)
    endif()
    if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_TFCARD_BINDING)
        idf_component_optional_requires(PRIVATE lunaredu_device_sdcard)
    endif()
    if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_SERVO_BINDING)
        idf_component_optional_requires(PRIVATE lunaredu_device_servo)
    endif()
    if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_SCREEN_BINDING)
        idf_component_optional_requires(PRIVATE lunaredu_device_spi_lcd)
    endif()
    if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_RGB_BINDING)
        idf_component_optional_requires(PRIVATE lunaredu_device_ws2812)
    endif()
    if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_IMU_BINDING)
        idf_component_optional_requires(PRIVATE lunaredu_device_mpu6050)
    endif()
endif()

if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_VISION_BINDINGS)
    idf_component_optional_requires(PRIVATE service_vision
                                    image_codec)
endif()

if(CONFIG_LUNAREDU_MICROPYTHON_ENABLE_LUNARLINK_BINDINGS)
    idf_component_optional_requires(PRIVATE service_assistant)
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
