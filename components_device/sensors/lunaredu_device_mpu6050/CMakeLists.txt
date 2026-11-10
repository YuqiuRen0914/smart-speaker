set(device_sources
    src/disabled.cpp
)
if(CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050)
    list(APPEND device_sources
        src/sampling_owner.cpp
        src/mpu6050_binding.cpp
        src/mpu6050_dispatch.cpp
        src/mpu6050_instance_module.cpp
        src/mpu6050_module.cpp
        src/mpu6050_payload.cpp
    )
endif()

idf_component_register(
    SRCS src/example.cpp ${device_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_driver_i2c core bblanchon__arduinojson
    PRIV_REQUIRES device_api esp_driver_gpio freertos esp_timer bsp
)

if(CONFIG_LUNAREDU_DEVICE_ENABLE_MPU6050)
    idf_component_optional_requires(PRIVATE esp_mpu6050 i2c_bus)
    include("${CMAKE_CURRENT_LIST_DIR}/../../../tools/cmake/mpu6050-ready-poll.cmake")
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL lunaros_fix_mpu6050_ready_poll)
endif()

if(device_sources)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
endif()

