# Apply repository-owned third-party build fixes after project() has created
# the IDF targets. These must not depend on selecting a particular peripheral.
# IDF's root ELF source inherits every public component include/definition.
# Large compositions exceed Windows CreateProcess's command-line limit even
# though each component compiles successfully. Enable CMake's response files
# here, after component subdirectories were configured, so the root target is
# protected without changing the existing component compilation commands.
if(CMAKE_HOST_WIN32 AND CMAKE_GENERATOR MATCHES "^Ninja")
    set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON)
endif()

if(CONFIG_GPROF_ENABLE)
    idf_build_get_property(lunaros_gprof_script LUNAROS_GPROF_BUILD_SCRIPT)
    if(NOT lunaros_gprof_script OR NOT EXISTS "${lunaros_gprof_script}")
        message(FATAL_ERROR "GProf policy requires its verified official port")
    endif()
    include("${lunaros_gprof_script}")
endif()
if(TARGET __idf_espressif__esp-dsp)
    target_compile_options(__idf_espressif__esp-dsp PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:-include>"
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_LIST_DIR}/esp_dsp_cmath_compat.h>"
    )
endif()

# Xtensa GCC 15.2.0 on Windows reproducibly crashes in IRA when compiling the
# IDF RGB bitmap driver at -Og (seen with the Yuezai debug configuration).
# Keep this workaround on that one translation unit and toolchain combination;
# application debug settings and repository/vendor sources remain unchanged.
if(CMAKE_HOST_WIN32 AND CONFIG_IDF_TARGET_ESP32S3 AND
   CONFIG_COMPILER_OPTIMIZATION_DEBUG AND CMAKE_C_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_C_COMPILER_VERSION VERSION_EQUAL "15.2.0" AND TARGET __idf_esp_lcd)
    set_property(SOURCE "$ENV{IDF_PATH}/components/esp_lcd/rgb/esp_lcd_panel_rgb.c"
        TARGET_DIRECTORY __idf_esp_lcd APPEND PROPERTY COMPILE_OPTIONS "-O2")
endif()
