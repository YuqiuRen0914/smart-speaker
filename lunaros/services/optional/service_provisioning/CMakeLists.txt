if(CMAKE_SCRIPT_MODE_FILE)
    file(GLOB provisioning_sources src/*.cpp)
else()
    file(GLOB provisioning_sources CONFIGURE_DEPENDS src/*.cpp)
endif()
idf_component_register(SRCS ${provisioning_sources} INCLUDE_DIRS include PRIV_INCLUDE_DIRS src
    REQUIRES device_api  core service_network device_protocol
    PRIV_REQUIRES configuration esp_timer esp_hw_support digest network_provisioning_codec protocomm mbedtls)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
