include(${CMAKE_CURRENT_LIST_DIR}/src/subsystems.cmake)

if(CONFIG_LUNAREDU_DEVICE_WEB)
    list(REMOVE_ITEM lunar_sources "${CMAKE_CURRENT_LIST_DIR}/src/device_web/portal_disabled.cpp")
else()
    list(REMOVE_ITEM lunar_sources "${CMAKE_CURRENT_LIST_DIR}/src/device_web/portal.cpp")
endif()

# LunarOS is one ESP-IDF component. Internal domain boundaries live below src/
# and are enforced by private headers, ownership checks and host tests rather
# than by a family of wrapper components.
idf_component_register(
    SRCS ${lunar_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES
        bblanchon__arduinojson
        freertos
        log
    PRIV_REQUIRES
        app_update
        esp_app_format
        esp_event
        esp_http_client
        esp_http_server
        esp_hw_support
        esp_netif
        esp_system
        esp_timer
        esp-tls
        esp_wifi
        bt
        heap
        joltwallet__littlefs
        lunaredu_bsp
        lwip
        mbedtls
        nvs_flash
        lunaredu_nvs
        spi_flash
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)

if(CONFIG_LUNAREDU_DEVICE_WEB)
    set(page_source "${CMAKE_CURRENT_LIST_DIR}/src/device_web/portal.html")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${page_source}")
    set(page_script "${CMAKE_CURRENT_LIST_DIR}/src/device_web/portal.js")
    set(page_style "${CMAKE_CURRENT_LIST_DIR}/src/device_web/portal.css")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${page_script}" "${page_style}")
    file(READ "${page_source}" page_content)
    file(READ "${page_script}" page_js)
    file(READ "${page_style}" page_css)
    string(REPLACE "/* PORTAL_SCRIPT */" "${page_js}" page_content "${page_content}")
    string(REPLACE "/* PORTAL_STYLE */" "${page_css}" page_content "${page_content}")
    string(REGEX REPLACE "\n[ \t]+" "\n" page_content "${page_content}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/portal.html" "${page_content}")
    target_add_binary_data(${COMPONENT_LIB}
        "${CMAKE_CURRENT_BINARY_DIR}/portal.html" TEXT)
endif()

if(CONFIG_LUNAREDU_DESKTOP_LOCAL OR CONFIG_LUNAREDU_DESKTOP_SHARED_FONTS)
    idf_component_optional_requires(PUBLIC lvgl__lvgl)
elseif(CONFIG_LUNAREDU_DESKTOP_LVGL_RUNTIME)
    idf_component_optional_requires(PRIVATE lvgl__lvgl)
endif()

# Metadata serialization and settings transactions are cold paths. Preserve the
# product's debug/size modes and avoid overriding its realtime optimization.
if(CONFIG_COMPILER_OPTIMIZATION_PERF)
    foreach(source IN LISTS lunar_sources)
        if(source MATCHES "/device_api/" OR source MATCHES "/persistence/settings/")
            set_property(SOURCE ${source} APPEND PROPERTY COMPILE_OPTIONS "-Os")
        endif()
    endforeach()
endif()
