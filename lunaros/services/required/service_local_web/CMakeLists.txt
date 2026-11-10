set(local_web_root "${CMAKE_CURRENT_LIST_DIR}")
if(CMAKE_SCRIPT_MODE_FILE)
    file(GLOB_RECURSE local_web_sources
        "${local_web_root}/src/device_api/*.cpp")
    file(GLOB local_web_page_sources
        "${local_web_root}/src/device_web/*.cpp")
else()
    file(GLOB_RECURSE local_web_sources CONFIGURE_DEPENDS
        "${local_web_root}/src/device_api/*.cpp")
    file(GLOB local_web_page_sources CONFIGURE_DEPENDS
        "${local_web_root}/src/device_web/*.cpp")
endif()
if(CONFIG_LUNAREDU_DEVICE_WEB)
    function(embed_web_asset source)
        get_filename_component(asset_name "${source}" NAME)
        set(asset_dir "${CMAKE_CURRENT_BINARY_DIR}/gzip")
        file(MAKE_DIRECTORY "${asset_dir}")
        set(asset_output "${asset_dir}/${asset_name}")
        idf_build_get_property(asset_python PYTHON)
        idf_build_get_property(asset_project PROJECT_DIR)
        set(asset_tool "${asset_project}/tools/assets/gzip_asset.py")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${asset_tool}")
        execute_process(COMMAND "${asset_python}"
            "${asset_tool}" "${source}" "${asset_output}"
            TIMEOUT 60 COMMAND_ERROR_IS_FATAL ANY)
        # Preserve symbol names; BINARY does not append a TEXT terminator.
        target_add_binary_data(${COMPONENT_LIB} "${asset_output}" BINARY)
    endfunction()
    list(REMOVE_ITEM local_web_page_sources
        "${local_web_root}/src/device_web/portal_disabled.cpp")
else()
    list(REMOVE_ITEM local_web_page_sources
        "${local_web_root}/src/device_web/portal.cpp")
endif()
list(APPEND local_web_sources ${local_web_page_sources})

# OS implementations still compile in the parent component.
idf_component_register(
    SRCS ${local_web_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES device_api service_network bblanchon__arduinojson freertos log
    PRIV_REQUIRES device_api_application application configuration service_ota core device_protocol service_platform_link service_storage
        esp_http_server esp_timer esp_system esp_app_format heap lwip digest mbedtls
)
if(NOT CMAKE_SCRIPT_MODE_FILE AND NOT CONFIG_MBEDTLS_ASN1_WRITE_C)
    message(FATAL_ERROR "Local Web maintenance JWKS requires the official mbedTLS ASN.1 writer")
endif()
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)

if(CONFIG_LUNAREDU_DEVICE_WEB)
    set(page_source "${local_web_root}/src/device_web/portal.html")
    set(page_script "${local_web_root}/src/device_web/portal.js")
    set(page_style "${local_web_root}/src/device_web/portal.css")
    set(client_script "${local_web_root}/src/device_web/device-client.js")
    set(application_style "${local_web_root}/src/device_web/application.css")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${page_source}" "${page_script}" "${page_style}" "${application_style}" "${client_script}")
    file(READ "${page_source}" page_content)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/portal.html" "${page_content}")
    embed_web_asset("${CMAKE_CURRENT_BINARY_DIR}/portal.html")
    embed_web_asset("${page_style}")
    embed_web_asset("${page_script}")
    embed_web_asset("${application_style}")
    embed_web_asset("${client_script}")

    set(factory_source "${local_web_root}/src/device_web/factory.html")
    set(factory_style "${local_web_root}/src/device_web/factory.css")
    set(factory_script "${local_web_root}/src/device_web/factory.js")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${factory_source}" "${factory_style}" "${factory_script}")
    file(READ "${factory_source}" factory_content)
    file(READ "${factory_style}" factory_style_content)
    file(READ "${factory_script}" factory_script_content)
    string(REPLACE "<!-- FACTORY_STYLES -->"
        "<link rel=\"stylesheet\" href=\"/portal.css\">\n<style>${factory_style_content}</style>"
        factory_content "${factory_content}")
    string(REPLACE "<!-- FACTORY_SCRIPT -->"
        "<script>${factory_script_content}</script>"
        factory_content "${factory_content}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/factory.html" "${factory_content}")
    embed_web_asset("${CMAKE_CURRENT_BINARY_DIR}/factory.html")
endif()

if(CONFIG_COMPILER_OPTIMIZATION_PERF)
    foreach(source IN LISTS local_web_sources)
        if(source MATCHES "/device_api/")
            set_property(SOURCE ${source} APPEND PROPERTY COMPILE_OPTIONS "-Os")
        endif()
    endforeach()
endif()

idf_build_get_property(local_web_components BUILD_COMPONENTS)
if("service_provisioning" IN_LIST local_web_components)
 idf_component_optional_requires(PRIVATE service_provisioning)
 target_compile_definitions(${COMPONENT_LIB} PRIVATE LUNAROS_LOCAL_WEB_PROVISIONING_COMPONENT=1)
endif()
