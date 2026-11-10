set(resource_root "${CMAKE_CURRENT_LIST_DIR}")
if(CMAKE_SCRIPT_MODE_FILE)
    file(GLOB resource_sources "${resource_root}/src/*.cpp")
else()
    file(GLOB resource_sources CONFIGURE_DEPENDS
        "${resource_root}/src/*.cpp")
endif()

idf_component_register(
    SRCS ${resource_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES device_api core
    PRIV_REQUIRES service_network freertos digest service_download service_storage
        bblanchon__arduinojson heap esp_rom mbedtls
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)

# Resource installation, validation and JSON metadata run on management paths;
# storage, network transfer and cryptographic primitives keep their own policy.
if(CONFIG_COMPILER_OPTIMIZATION_PERF)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Os)
endif()
