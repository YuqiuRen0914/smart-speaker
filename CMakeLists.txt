cmake_minimum_required(VERSION 3.16.0)

# Compatibility names used by the extracted components always refer to this
# checkout. No SDK root, parent repository discovery or external source path.
set(LUNAREDU_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(LUNAREDU_APP_NAME "smart-speaker")
set(LUNAREDU_APP_DIR "${CMAKE_CURRENT_LIST_DIR}")
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
endif()
if(DEFINED LUNAREDU_EXTRA_SDKCONFIG_DEFAULTS)
    list(APPEND SDKCONFIG_DEFAULTS ${LUNAREDU_EXTRA_SDKCONFIG_DEFAULTS})
endif()
include("${CMAKE_CURRENT_LIST_DIR}/product.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/components.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/storage.cmake")
list(APPEND EXCLUDE_COMPONENTS wifi_provisioning)
set(ENV{IDF_COMPONENT_CONSTRAINT_FILES} "${CMAKE_CURRENT_LIST_DIR}/cmake/dependency-constraints.txt")
set(COMPONENTS main esp_http_client)
# Include selected peripheral Kconfig even for optional runtime adapters.
foreach(component IN LISTS LUNAROS_PRODUCT_COMPONENTS)
    if(component MATCHES "^lunaredu_device_")
        list(APPEND COMPONENTS "${component}")
    endif()
endforeach()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lunaredu-${LUNAREDU_APP_NAME})

if(NOT "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}" VERSION_EQUAL "6.0.1")
    message(FATAL_ERROR "This release requires ESP-IDF 6.0.1")
endif()


if(TARGET __idf_espressif__esp-dsp)
    target_compile_options(__idf_espressif__esp-dsp PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-include>" "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_LIST_DIR}/cmake/esp_dsp_cmath_compat.h>")
endif()
