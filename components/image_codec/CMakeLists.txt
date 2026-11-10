set(lunaros_image_codec_sources
    src/image_jpeg_decoder.cpp
    src/image_jpeg_encoder.cpp
    src/image_jpeg_encoder_sw.cpp
    src/image_rgb565_transform.cpp
)

if(CONFIG_LUNAROS_IMAGE_CODEC_ENABLE_HARDWARE_JPEG_ENCODER)
    list(APPEND lunaros_image_codec_sources src/image_jpeg_encoder_hw.cpp)
endif()

idf_component_register(
    SRCS
        ${lunaros_image_codec_sources}
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "src"
    PRIV_REQUIRES
        espressif__esp_image_effects
        espressif__esp_new_jpeg
        heap
        log
)

if(CONFIG_LUNAROS_IMAGE_CODEC_ENABLE_HARDWARE_JPEG_ENCODER)
    idf_component_optional_requires(PRIVATE esp_driver_jpeg)
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-shadow -Wno-conversion)
