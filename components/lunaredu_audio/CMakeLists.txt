set(audio_sources src/disabled.cpp)
# IDF discovers dependencies before product Kconfig is loaded. Keep the backend
# dependency in the graph; only its adapter is conditionally compiled below.
set(audio_requires esp_common log lunaredu_device_i2s_audio)
if(CONFIG_LUNAREDU_DEVICE_ENABLE_AUDIO)
    list(APPEND audio_sources src/local_pcm_audio_bridge.cpp src/backends/i2s.cpp)
endif()
idf_component_register(SRCS ${audio_sources} INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS src PRIV_REQUIRES ${audio_requires})
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
