#include <sdkconfig.h>
#include <cstdlib>
#include <cstring>
#include <esp_heap_caps.h>
#include "image_jpeg_encoder_internal.h"
#include "image_input_validation.h"

bool encode_image_to_jpeg(const uint8_t *src, size_t src_len, uint16_t width,
                          uint16_t height, ImagePixelFormat format,
                          uint8_t quality, uint8_t **out, size_t *out_len) {
  if (out != nullptr) { *out = nullptr; }
  if (out_len != nullptr) { *out_len = 0; }
  if (out == nullptr || out_len == nullptr ||
      !valid_image_input(src, src_len, width, height, format)) {
    return false;
  }
  if (format == ImagePixelFormat::Jpeg) {
#if CONFIG_LUNAROS_IMAGE_CODEC_ALLOW_JPEG_INPUT
    auto *data = static_cast<uint8_t *>(heap_caps_malloc(src_len, MALLOC_CAP_8BIT));
    if (data == nullptr) { return false; }
    std::memcpy(data, src, src_len);
    *out = data;
    *out_len = src_len;
    return true;
#else
    return false;
#endif
  }
#if CONFIG_LUNAROS_IMAGE_CODEC_ENABLE_HARDWARE_JPEG_ENCODER
  if (encode_jpeg_with_hardware_backend(src, src_len, width, height, format,
                                        quality, out, out_len, nullptr, nullptr)) {
    return true;
  }
#endif
  return encode_jpeg_with_software_backend(src, src_len, width, height, format,
                                           quality, out, out_len, nullptr, nullptr);
}

bool encode_image_to_jpeg_cb(const uint8_t *src, size_t src_len, uint16_t width,
                             uint16_t height, ImagePixelFormat format,
                             uint8_t quality, jpeg_write_cb cb, void *arg) {
  if (!cb || !valid_image_input(src,src_len,width,height,format))return false;
  if(format==ImagePixelFormat::Jpeg){
#if CONFIG_LUNAROS_IMAGE_CODEC_ALLOW_JPEG_INPUT
    return cb(arg,0,src,src_len)==src_len&&cb(arg,src_len,nullptr,0)==0;
#else
    return false;
#endif
  }
  struct Sink {jpeg_write_cb callback;void *context;bool emitted=false;} sink{cb,arg};
  const auto forward=[](void *opaque,size_t offset,const void *data,size_t size)->size_t {
    auto &s=*static_cast<Sink *>(opaque);s.emitted|=size!=0;return s.callback(s.context,offset,data,size);
  };
#if CONFIG_LUNAROS_IMAGE_CODEC_ENABLE_HARDWARE_JPEG_ENCODER
  if(encode_jpeg_with_hardware_backend(src,src_len,width,height,format,quality,nullptr,nullptr,forward,&sink))return true;
  if(sink.emitted)return false; // Never append a second encoding after a partial sink write.
#endif
  return encode_jpeg_with_software_backend(src,src_len,width,height,format,quality,nullptr,nullptr,forward,&sink);
}
