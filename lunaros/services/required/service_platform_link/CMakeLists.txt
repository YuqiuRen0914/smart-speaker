set(platform_link_root "${CMAKE_CURRENT_LIST_DIR}")
if(CMAKE_SCRIPT_MODE_FILE)
    file(GLOB platform_link_sources "${platform_link_root}/src/*.cpp")
else()
    file(GLOB platform_link_sources CONFIGURE_DEPENDS
        "${platform_link_root}/src/*.cpp")
endif()

idf_component_register(
    SRCS ${platform_link_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES device_api service_network bblanchon__arduinojson freertos
    PRIV_REQUIRES device_api_application application configuration device_protocol core service_resource service_ota heap espressif__mqtt esp-tls mbedtls
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)

if(CONFIG_COMPILER_OPTIMIZATION_PERF)
    # Binding, persistence and JSON management favor size. Preserve the
    # realtime command channel's existing speed optimization explicitly.
    target_compile_options(${COMPONENT_LIB} PRIVATE -Os)
    set_source_files_properties(src/platform_link_realtime.cpp PROPERTIES COMPILE_OPTIONS "-O2")
endif()
