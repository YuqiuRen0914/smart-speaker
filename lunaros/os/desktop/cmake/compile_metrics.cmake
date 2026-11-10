# Product physical values are resolved once, before ESP-IDF creates targets.
function(lunaros_compile_ui_metrics)
    cmake_parse_arguments(UI "" "SIDE;ACTIVE_WIDTH_UM;TEXT_PERCENT" "" ${ARGN})
    if(UI_UNPARSED_ARGUMENTS OR NOT UI_SIDE MATCHES "^[0-9]+$" OR
       NOT UI_ACTIVE_WIDTH_UM MATCHES "^[0-9]+$")
        message(FATAL_ERROR "UI requires SIDE and ACTIVE_WIDTH_UM integer values")
    endif()
    if(UI_SIDE LESS 240 OR UI_SIDE GREATER 800 OR
       UI_ACTIVE_WIDTH_UM LESS 20000 OR UI_ACTIVE_WIDTH_UM GREATER 200000)
        message(FATAL_ERROR "UI active display dimensions are out of range")
    endif()
    if("${UI_TEXT_PERCENT}" STREQUAL "")
        set(UI_TEXT_PERCENT 100)
    endif()
    if(NOT UI_TEXT_PERCENT MATCHES "^[0-9]+$" OR UI_TEXT_PERCENT LESS 80 OR UI_TEXT_PERCENT GREATER 160)
        message(FATAL_ERROR "UI text scale must be 80..160 percent")
    endif()
    # ESP-IDF also reads product selection in its script-only dependency pass.
    # That pass validates the profile; targets inherit it from the root build.
    if(NOT CMAKE_SCRIPT_MODE_FILE)
        add_compile_definitions(LUNAROS_UI_SIDE=${UI_SIDE}
            LUNAROS_UI_ACTIVE_WIDTH_UM=${UI_ACTIVE_WIDTH_UM}
            LUNAROS_UI_TEXT_PERCENT=${UI_TEXT_PERCENT})
    endif()
endfunction()
