#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>

#include <esp_heap_caps.h>
#include <esp_log.h>

#include "driver/jpeg_encode.h"

#include "image_jpeg_encoder_internal.h"

namespace {

constexpr const char *kLogTag = "encode_image_to_jpeg";

static void *malloc_psram(size_t size) {
  void *p = malloc(size);
  if (p != nullptr) {
    return p;
  }
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static jpeg_encoder_handle_t s_hw_jpeg_handle = nullptr;

static bool hw_jpeg_ensure_inited(void) {
  if (s_hw_jpeg_handle != nullptr) {
    return true;
  }

  jpeg_encode_engine_cfg_t eng_cfg = {};
  eng_cfg.intr_priority = 0;
  eng_cfg.timeout_ms = 100;
  const esp_err_t er = jpeg_new_encoder_engine(&eng_cfg, &s_hw_jpeg_handle);
  if (er != ESP_OK) {
    ESP_LOGE(kLogTag, "jpeg_new_encoder_engine failed: %d",
             static_cast<int>(er));
    s_hw_jpeg_handle = nullptr;
    return false;
  }
  return true;
}

static uint8_t *convert_input_to_hw_encoder_buf(const uint8_t *src,
                                                uint16_t width,
                                                uint16_t height,
                                                ImagePixelFormat format,
                                                jpeg_enc_input_format_t *out_fmt,
                                                int *out_size) {
  if (format == ImagePixelFormat::Gray8) {
    const int sz = static_cast<int>(width) * static_cast<int>(height);
    uint8_t *buf = static_cast<uint8_t *>(malloc_psram(sz));
    if (buf == nullptr) {
      return nullptr;
    }
    memcpy(buf, src, sz);
    if (out_fmt != nullptr) {
      *out_fmt = JPEG_ENCODE_IN_FORMAT_GRAY;
    }
    if (out_size != nullptr) {
      *out_size = sz;
    }
    return buf;
  }

  if (format == ImagePixelFormat::Rgb24) {
    const int sz = static_cast<int>(width) * static_cast<int>(height) * 3;
    uint8_t *buf = static_cast<uint8_t *>(malloc_psram(sz));
    if (buf == nullptr) {
      ESP_LOGE(kLogTag, "malloc_psram failed");
      return nullptr;
    }
    memcpy(buf, src, sz);
    if (out_fmt != nullptr) {
      *out_fmt = JPEG_ENCODE_IN_FORMAT_RGB888;
    }
    if (out_size != nullptr) {
      *out_size = sz;
    }
    return buf;
  }

  if (format == ImagePixelFormat::Rgb565) {
    const int sz = static_cast<int>(width) * static_cast<int>(height) * 2;
    uint8_t *buf = static_cast<uint8_t *>(malloc_psram(sz));
    if (buf == nullptr) {
      return nullptr;
    }
    memcpy(buf, src, sz);
    if (out_fmt != nullptr) {
      *out_fmt = JPEG_ENCODE_IN_FORMAT_RGB565;
    }
    if (out_size != nullptr) {
      *out_size = sz;
    }
    return buf;
  }

  if (format == ImagePixelFormat::Yuyv) {
    const int sz = static_cast<int>(width) * static_cast<int>(height) * 2;
    uint16_t *buf = static_cast<uint16_t *>(malloc_psram(sz));
    if (buf == nullptr) {
      return nullptr;
    }
    auto *bytes = reinterpret_cast<uint8_t *>(buf);
    for (int i = 0; i < sz; i += 2) {
      bytes[i] = src[i + 1];
      bytes[i + 1] = src[i];
    }
    if (out_fmt != nullptr) {
      *out_fmt = JPEG_ENCODE_IN_FORMAT_YUV422;
    }
    if (out_size != nullptr) {
      *out_size = sz;
    }
    return reinterpret_cast<uint8_t *>(buf);
  }

  return nullptr;
}

static bool encode_with_hw_jpeg(const uint8_t *src, size_t src_len,
                                uint16_t width, uint16_t height,
                                ImagePixelFormat format, uint8_t quality,
                                uint8_t **jpg_out, size_t *jpg_out_len,
                                jpeg_write_cb cb, void *cb_arg) {
  // The hardware engine is shared across callers.
  static std::mutex encoder_mutex;
  std::unique_lock<std::mutex> lock(encoder_mutex);
  (void)src_len;
  if (quality < 1) {
    quality = 1;
  } else if (quality > 100) {
    quality = 100;
  }

  jpeg_enc_input_format_t enc_src_type = JPEG_ENCODE_IN_FORMAT_RGB888;
  int enc_in_size = 0;
  uint8_t *enc_in = convert_input_to_hw_encoder_buf(
      src, width, height, format, &enc_src_type, &enc_in_size);
  if (enc_in == nullptr) {
    ESP_LOGW(kLogTag, "hw jpeg: unsupported format");
    return false;
  }

  if (!hw_jpeg_ensure_inited()) {
    free(enc_in);
    return false;
  }

  jpeg_encode_cfg_t enc_cfg = {};
  enc_cfg.width = width;
  enc_cfg.height = height;
  enc_cfg.src_type = enc_src_type;
  enc_cfg.image_quality = quality;
  enc_cfg.sub_sample = (enc_src_type == JPEG_ENCODE_IN_FORMAT_GRAY)
                           ? JPEG_DOWN_SAMPLING_GRAY
                           : JPEG_DOWN_SAMPLING_YUV422;

  size_t out_cap = static_cast<size_t>(width) * static_cast<size_t>(height) * 3 / 2 +
                   64 * 1024;
  if (out_cap < 128 * 1024) {
    out_cap = 128 * 1024;
  }
  jpeg_encode_memory_alloc_cfg_t jpeg_enc_output_mem_cfg = {
      .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
  };
  size_t out_cap_aligned = 0;
  uint8_t *outbuf = static_cast<uint8_t *>(
      jpeg_alloc_encoder_mem(out_cap, &jpeg_enc_output_mem_cfg, &out_cap_aligned));
  if (outbuf == nullptr) {
    free(enc_in);
    ESP_LOGE(kLogTag, "alloc out buffer failed");
    return false;
  }

  uint32_t out_len = 0;
  esp_err_t er = jpeg_encoder_process(s_hw_jpeg_handle, &enc_cfg, enc_in,
                                      static_cast<uint32_t>(enc_in_size),
                                      outbuf, static_cast<uint32_t>(out_cap_aligned),
                                      &out_len);
  free(enc_in);
  // The synchronous driver has finished; callbacks may start another encode.
  lock.unlock();
  if (er != ESP_OK || out_len == 0 || out_len > out_cap_aligned) {
    free(outbuf);
    ESP_LOGE(kLogTag, "jpeg_encoder_process failed: %d",
             static_cast<int>(er));
    return false;
  }

  if (cb != nullptr) {
    const bool complete = cb(cb_arg, 0, outbuf, static_cast<size_t>(out_len)) == out_len;
    const bool ended = complete && cb(cb_arg, out_len, nullptr, 0) == 0;
    free(outbuf);
    if (jpg_out != nullptr) {
      *jpg_out = nullptr;
    }
    if (jpg_out_len != nullptr) {
      *jpg_out_len = 0;
    }
    return ended;
  }

  if (jpg_out != nullptr && jpg_out_len != nullptr) {
    *jpg_out = outbuf;
    *jpg_out_len = static_cast<size_t>(out_len);
    return true;
  }

  free(outbuf);
  return true;
}

} // namespace

bool encode_jpeg_with_hardware_backend(const uint8_t *src, size_t src_len,
                                      uint16_t width, uint16_t height,
                                      ImagePixelFormat format, uint8_t quality,
                                      uint8_t **jpg_out, size_t *jpg_out_len,
                                      jpeg_write_cb cb, void *cb_arg) {
  return encode_with_hw_jpeg(src, src_len, width, height, format, quality,
                             jpg_out, jpg_out_len, cb, cb_arg);
}


