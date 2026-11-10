#pragma once

#include "image_jpeg_encoder.h"

bool encode_jpeg_with_software_backend(const uint8_t *src, size_t src_len,
                                       uint16_t width, uint16_t height,
                                       ImagePixelFormat format,
                                       uint8_t quality, uint8_t **jpg_out,
                                       size_t *jpg_out_len, jpeg_write_cb cb,
                                       void *cb_arg);

bool encode_jpeg_with_hardware_backend(const uint8_t *src, size_t src_len,
                                       uint16_t width, uint16_t height,
                                       ImagePixelFormat format,
                                       uint8_t quality, uint8_t **jpg_out,
                                       size_t *jpg_out_len, jpeg_write_cb cb,
                                       void *cb_arg);
