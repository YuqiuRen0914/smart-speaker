set(audio_sources src/audio_focus.cpp src/disabled.cpp src/volume.cpp src/pcm_resampler.cpp src/audio_file_decoder.cpp src/audio_library.cpp src/audio_import.cpp
    src/realtime/audio_dsp.cpp src/pcm_output_adapter.cpp src/audio_file_playback.cpp src/realtime/gmf_pcm_pipeline.cpp)
# IDF discovers dependencies before product Kconfig is loaded. Keep the backend
# dependency in the graph; only its adapter is conditionally compiled below.
set(audio_requires esp_common heap log esp_driver_gpio esp_driver_i2s freertos)
if(CONFIG_LUNAROS_AUDIO_FILE_PLAYER)
    list(APPEND audio_sources src/file_player.cpp)
endif()
if(CONFIG_LUNAROS_AUDIO_DUAL_I2S)
    list(APPEND audio_sources src/audio_history.cpp src/backends/dual_i2s.cpp src/backends/dual_i2s_lifecycle.cpp)
endif()
if(CONFIG_LUNAROS_AUDIO_RUNTIME)
    list(APPEND audio_sources src/local_pcm_audio_bridge.cpp)
endif()
if(CONFIG_LUNAREDU_DEVICE_ENABLE_AUDIO)
    list(APPEND audio_sources
        src/backends/i2s.cpp
        src/realtime/realtime_pcm_backend.cpp)
endif()
idf_component_register(SRCS ${audio_sources} INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS src REQUIRES core PRIV_REQUIRES configuration ${audio_requires} esp_timer
        espressif__esp-dsp espressif__esp_audio_effects espressif__esp_audio_codec espressif__esp_audio_render espressif__gmf_core espressif__gmf_audio)
if(CONFIG_LUNAREDU_DEVICE_ENABLE_AUDIO)
    idf_component_optional_requires(PRIVATE lunaredu_device_i2s_audio)
endif()
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
if(CONFIG_AUDIO_DECODER_MP3_SUPPORT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LUNAROS_AUDIO_MP3_CODEC=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LUNAROS_AUDIO_MP3_CODEC=0)
endif()
set_source_files_properties(src/realtime/audio_dsp.cpp PROPERTIES COMPILE_OPTIONS "-O2")
set_source_files_properties(src/backends/dual_i2s.cpp PROPERTIES COMPILE_OPTIONS "-O2")
if(CONFIG_LUNAREDU_DEVICE_ENABLE_AUDIO)
    set_source_files_properties(src/realtime/realtime_pcm_backend.cpp
        PROPERTIES COMPILE_OPTIONS "-O2")
endif()
