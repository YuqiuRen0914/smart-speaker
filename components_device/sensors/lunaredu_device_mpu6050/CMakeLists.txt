set(device_sources
    src/disabled.cpp
)
if(CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050)
    list(APPEND device_sources
        src/mpu6050_binding.cpp
        src/mpu6050_dispatch.cpp
        src/mpu6050_instance_module.cpp
        src/mpu6050_module.cpp
        src/mpu6050_payload.cpp
    )
endif()

idf_component_register(
    SRCS src/test.cpp ${device_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES bblanchon__arduinojson esp_timer lunaredu_bsp lunaros
)

if(CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050)
    idf_component_optional_requires(PRIVATE esp_mpu6050 i2c_bus)
endif()

if(device_sources)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
endif()
