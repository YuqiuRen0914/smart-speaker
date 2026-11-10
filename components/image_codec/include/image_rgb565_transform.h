#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
struct ImageCrop {uint16_t x=0,y=0,width=0,height=0;};
// Contiguous borrowed RGB565 LE source and owned output. Cropping/scaling uses
// esp_image_effects; at most 256 KiB crop scratch, no retained frame or worker.
// False never authorizes publishing a partially written destination.
bool transform_rgb565_image(const uint16_t *source,size_t source_pixels,uint16_t width,uint16_t height,
    ImageCrop crop,uint16_t *output,size_t output_pixels,uint16_t output_width,uint16_t output_height,
    bool smooth,std::string &error);
bool cleanup_rgb565_transforms();
