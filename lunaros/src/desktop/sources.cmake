set(lunar_desktop_sources "")
if(CONFIG_LUNAREDU_DESKTOP_LVGL_RUNTIME)
    list(APPEND lunar_desktop_sources "${CMAKE_CURRENT_LIST_DIR}/local/lvgl_screen_runtime.cpp")
else()
    list(APPEND lunar_desktop_sources "${CMAKE_CURRENT_LIST_DIR}/local/lvgl_screen_runtime_disabled.cpp")
endif()
if(CONFIG_LUNAREDU_DESKTOP_LOCAL OR CONFIG_LUNAREDU_DESKTOP_REMOTE OR CONFIG_LUNAREDU_DESKTOP_SHARED_FONTS)
    list(APPEND lunar_desktop_sources "${CMAKE_CURRENT_LIST_DIR}/core/utf8.cpp")
endif()
if(CONFIG_LUNAREDU_DESKTOP_SHARED_FONTS)
    list(APPEND lunar_desktop_sources "${CMAKE_CURRENT_LIST_DIR}/local/lunaros_font_runtime.cpp")
endif()
if(CONFIG_LUNAREDU_DESKTOP_LOCAL OR CONFIG_LUNAREDU_DESKTOP_REMOTE)
    list(APPEND lunar_desktop_sources
        "${CMAKE_CURRENT_LIST_DIR}/core/local_navigation.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/core/interaction_policy.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/core/page_state.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/core/resource_provider.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/core/ui_intent_queue.cpp"
    )
endif()
if(CONFIG_LUNAREDU_DESKTOP_LOCAL)
    list(APPEND lunar_desktop_sources
        "${CMAKE_CURRENT_LIST_DIR}/local/application_view.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/local/local_shell.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/local/lunaros_ui/lunaros_logo_assets.c"
        "${CMAKE_CURRENT_LIST_DIR}/local/lunaros_ui/lunaros_ui_draw.c"
        "${CMAKE_CURRENT_LIST_DIR}/local/lunaros_ui/lunaros_boot_view.c"
        "${CMAKE_CURRENT_LIST_DIR}/local/lunaros_ui_port_esp32.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/local/lvgl_shell_view.cpp"
    )
endif()
if(CONFIG_LUNAREDU_DESKTOP_REMOTE)
    list(APPEND lunar_desktop_sources
        "${CMAKE_CURRENT_LIST_DIR}/remote/document_widgets.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/remote/file_document_widget.cpp"
    )
endif()
