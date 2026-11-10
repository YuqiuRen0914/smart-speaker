set(app_local_view_sources "")
if(CONFIG_LUNAREDU_DESKTOP_LOCAL)
    list(APPEND app_local_view_sources "src/views/desktop_view.c" "src/views/subtitle_policy.cpp" "src/views/desktop_view_adapter.cpp")
endif()
idf_component_register(
    SRCS ${app_local_view_sources}
        "src/actions/capability_client.cpp"
        "src/app/capabilities.cpp"
        "src/app/conversation_projection.cpp"
        "src/app/snapshot.cpp"
        "src/behavior/behavior_state.cpp"
        "src/behavior/emotion.cpp"
        "src/interaction/conversation/assistant_client_id.cpp"
        "src/interaction/conversation/assistant_core.cpp"
        "src/interaction/conversation/assistant_entry_policy.cpp"
        "src/interaction/conversation/assistant_lifecycle.cpp"
        "src/interaction/conversation/assistant_loop.cpp"
        "src/interaction/conversation/assistant_policy.cpp"
        "src/interaction/conversation/assistant_runtime_persist.cpp"
        "src/interaction/conversation/assistant_runtime_reset.cpp"
        "src/interaction/conversation/assistant_session_flow.cpp"
        "src/interaction/conversation/assistant_session_server.cpp"
        "src/interaction/conversation/assistant_session_state.cpp"
        "src/interaction/conversation/assistant_session_transport.cpp"
        "src/interaction/conversation/assistant_state_text.cpp"
        "src/interaction/conversation/host_events.cpp"
        "src/interaction/conversation/phase.cpp"
        "src/interaction/conversation/product_system_state.cpp"
        "src/interaction/conversation/service.cpp"
        "src/interaction/input.cpp"
        "src/interaction/tools/application_tools.cpp"
        "src/interaction/tools/assistant_mcp.cpp"
        "src/interaction/transport/assistant_activation_client.cpp"
        "src/interaction/transport/mqtt_client_support.cc"
        "src/interaction/transport/mqtt_session.cpp"
        "src/interaction/transport/network_xiaozhi_mqtt_udp_channel.cpp"
        "src/interaction/transport/network_xiaozhi_protocol.cpp"
        "src/interaction/transport/udp_channel.cpp"
        "src/interaction/voice/assistant_audio.cpp"
        "src/interaction/voice/local_audio_app.cpp"
        "src/interaction/voice/opus_audio_codec.cpp"
        "src/interaction/voice/wake_word_frontend.cpp"
        "src/runtime/runtime.cpp"
        "src/runtime/service.cpp"
        "src/state/pet_state.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        bblanchon__arduinojson
        lunaros
    PRIV_INCLUDE_DIRS
        "src"
    PRIV_REQUIRES
        espressif__esp_audio_codec
        espressif__esp-sr
        esp_hw_support
        esp-tls
        espressif__mqtt
        lwip
        mbedtls
        lunaredu_bsp
        lunaredu_audio
        nvs_flash
        esp_timer
        freertos
        log
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++20>"
    -Wno-shadow -Wno-conversion
)

if(CONFIG_LUNAREDU_DESKTOP_LOCAL)
    idf_component_optional_requires(PUBLIC lvgl__lvgl)
endif()
