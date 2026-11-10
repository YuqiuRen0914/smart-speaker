set(imu_service_sources src/imu_service.cpp src/imu_fusion_service.cpp)
if(CONFIG_LUNAROS_IMU_FUSION)
    list(APPEND imu_service_sources src/fusion/filter.cpp)
endif()
idf_component_register(
    SRCS ${imu_service_sources}
    INCLUDE_DIRS "include"
    REQUIRES core freertos esp_timer
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
