include_guard(GLOBAL)
get_filename_component(LUNAROS_STARTUP_SOURCE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../lunaros/bsp/startup" ABSOLUTE)

# Resolve the application's Kconfig once. Both images compile this same table;
# the bootloader does not need to import a product's unrelated Kconfig menus.
function(lunaros_generate_startup_outputs declarations target output_dir)
  if(NOT target STREQUAL "esp32s3")
    message(FATAL_ERROR "LunarOS startup safe outputs currently support esp32s3 only")
  endif()
  set(outputs "")
  set(pins "")
  foreach(entry IN LISTS declarations)
    if(NOT entry MATCHES "^(CONFIG_[A-Z0-9_]+):([01]):(none|up|down)$")
      message(FATAL_ERROR "Invalid LunarOS startup output declaration: ${entry}")
    endif()
    set(symbol "${CMAKE_MATCH_1}")
    set(level "${CMAKE_MATCH_2}")
    set(pull "${CMAKE_MATCH_3}")
    if(NOT DEFINED ${symbol} OR NOT "${${symbol}}" MATCHES "^(-1|0|[1-9][0-9]*)$")
      message(FATAL_ERROR "Missing or non-integer startup GPIO: ${symbol}")
    endif()
    set(pin "${${symbol}}")
    if((level EQUAL 0 AND pull STREQUAL "up") OR
        (level EQUAL 1 AND pull STREQUAL "down"))
      message(FATAL_ERROR "Startup pull opposes safe level: ${entry}")
    endif()
    if(pin EQUAL -1)
      continue() # Explicitly unassigned; do not touch any pad.
    endif()
    # ESP32-S3 product-output policy: exclude strap, USB and module memory
    # pads. A new chip/board policy needs an explicit review.
    if(NOT (pin EQUAL 1 OR pin EQUAL 2 OR (pin GREATER_EQUAL 4 AND pin LESS_EQUAL 18)
        OR pin EQUAL 21 OR (pin GREATER_EQUAL 38 AND pin LESS_EQUAL 44)
        OR pin EQUAL 47 OR pin EQUAL 48))
      message(FATAL_ERROR "Unsupported startup GPIO ${pin}: ${symbol}")
    endif()
    if(pin IN_LIST pins)
      message(FATAL_ERROR "Duplicate startup GPIO ${pin}: ${symbol}")
    endif()
    list(APPEND pins "${pin}")
    string(TOUPPER "${pull}" pull)
    string(APPEND outputs "    X(${pin}, ${level}, LUNAROS_STARTUP_PULL_${pull}) \\\n")
  endforeach()
  set(LUNAROS_STARTUP_OUTPUT_TABLE "${outputs}")
  file(MAKE_DIRECTORY "${output_dir}")
  configure_file("${LUNAROS_STARTUP_SOURCE_ROOT}/outputs.h.in"
      "${output_dir}/lunaros_startup_outputs.h" @ONLY)
  configure_file("${LUNAROS_STARTUP_SOURCE_ROOT}/bootloader.cmake.in"
      "${output_dir}/CMakeLists.txt" @ONLY)
endfunction()

# Register before IDF creates its bootloader ExternalProject. The numeric GPIO
# table is generated later by BSP, once application Kconfig has been resolved.
function(lunaros_prepare_startup_bootloader)
  get_property(prepared GLOBAL PROPERTY LUNAROS_STARTUP_BOOTLOADER_DIR)
  if(prepared OR NOT DEFINED LUNAREDU_APP_DIR)
    return()
  endif()
  unset(LUNAROS_STARTUP_SAFE_OUTPUTS)
  if(EXISTS "${LUNAREDU_APP_DIR}/product.cmake")
    include("${LUNAREDU_APP_DIR}/product.cmake")
  endif()
  if(NOT LUNAROS_STARTUP_SAFE_OUTPUTS AND NOT LUNAROS_BUILD_PROFILE STREQUAL "production-secure")
    return()
  endif()
  set(generated "${CMAKE_BINARY_DIR}/lunaros_startup_bootloader")
  file(MAKE_DIRECTORY "${generated}")
  configure_file("${LUNAROS_STARTUP_SOURCE_ROOT}/bootloader.cmake.in"
      "${generated}/CMakeLists.txt" @ONLY)
  if(NOT LUNAROS_STARTUP_SAFE_OUTPUTS)
    set(LUNAROS_STARTUP_OUTPUT_TABLE "")
    configure_file("${LUNAROS_STARTUP_SOURCE_ROOT}/outputs.h.in" "${generated}/lunaros_startup_outputs.h" @ONLY)
  endif()
  set_property(GLOBAL PROPERTY LUNAROS_STARTUP_BOOTLOADER_DIR "${generated}")
  idf_build_set_property(BOOTLOADER_EXTRA_COMPONENT_DIRS "${generated}" APPEND)
endfunction()

# BSP is automatically selected by LunarOS Runtime. Only declared products get
# either startup hook; products without a declaration retain their entrypoint.
function(lunaros_configure_startup component_lib)
  if(NOT DEFINED LUNAREDU_APP_DIR)
    return()
  endif()
  unset(LUNAROS_STARTUP_SAFE_OUTPUTS)
  if(EXISTS "${LUNAREDU_APP_DIR}/product.cmake")
    include("${LUNAREDU_APP_DIR}/product.cmake")
  endif()
  if(NOT LUNAROS_STARTUP_SAFE_OUTPUTS AND NOT LUNAROS_BUILD_PROFILE STREQUAL "production-secure")
    return()
  endif()
  idf_build_get_property(build_dir BUILD_DIR)
  idf_build_get_property(target IDF_TARGET)
  set(generated "${build_dir}/lunaros_startup_bootloader")
  get_property(registered GLOBAL PROPERTY LUNAROS_STARTUP_BOOTLOADER_DIR)
  if(NOT registered STREQUAL generated)
    message(FATAL_ERROR "Startup outputs require the shared LunarOS project build entry")
  endif()
  if(EXISTS "${LUNAREDU_APP_DIR}/product.cmake")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LUNAREDU_APP_DIR}/product.cmake")
  endif()
  lunaros_generate_startup_outputs("${LUNAROS_STARTUP_SAFE_OUTPUTS}" "${target}" "${generated}")
  target_sources(${component_lib} PRIVATE
      "${LUNAROS_STARTUP_SOURCE_ROOT}/safe_outputs.c"
      "${LUNAROS_STARTUP_SOURCE_ROOT}/app_entry.c")
  target_include_directories(${component_lib} PRIVATE "${generated}")
  # esp_system calls app_main in another translation unit. Wrapping it runs the
  # safety table before the product entrypoint, including applications updated by OTA.
  target_link_libraries(${component_lib} INTERFACE
      "-Wl,--wrap=app_main" "-Wl,-u,__wrap_app_main")
endfunction()
