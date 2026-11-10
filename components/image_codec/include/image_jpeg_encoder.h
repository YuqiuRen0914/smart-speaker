#pragma once

#include <stddef.h>
#include <stdint.h>

enum class ImagePixelFormat : uint32_t {
  Rgb565 = 0x50424752u,
  Rgb565ByteSwapped = 0x52474250u,
  Rgb24 = 0x33424752u,
  Yuyv = 0x56595559u,
  Yuv422Planar = 0x36315559u,
  Yuv420Planar = 0x32315559u,
  Gray8 = 0x59455247u,
  Uyvy = 0x59565955u,
  Jpeg = 0x4745504Au,
};

typedef size_t (*jpeg_write_cb)(void *arg, size_t index, const void *data,
                                size_t len);

// On success, release *out with free(). Failure clears both output fields.
// Color inputs require even dimensions; Gray8 does not. Source length is validated.
bool encode_image_to_jpeg(const uint8_t *src, size_t src_len, uint16_t width,
                          uint16_t height, ImagePixelFormat format,
                          uint8_t quality, uint8_t **out, size_t *out_len);

// Callback index is a byte offset. Return the number of accepted bytes.
// A short write fails; the final (nullptr, 0) notification is sent only on success.
// Encoding currently uses a complete temporary JPEG buffer, not streaming compression.
bool encode_image_to_jpeg_cb(const uint8_t *src, size_t src_len, uint16_t width,
                             uint16_t height, ImagePixelFormat format,
                             uint8_t quality, jpeg_write_cb cb, void *arg);

// Explicit software encoder owner: one input/conversion/encoder/output workspace.
// No hidden global cache. Caller retires it when its camera/upload session ends.
// Idle input <=150 KiB, output <=128 KiB; larger operations remain supported cold.
class JpegEncoderWorkspace {
 public:
  JpegEncoderWorkspace()=default;
  ~JpegEncoderWorkspace(){retire();}
  JpegEncoderWorkspace(const JpegEncoderWorkspace &)=delete;
  JpegEncoderWorkspace &operator=(const JpegEncoderWorkspace &)=delete;
  bool encode(const uint8_t *,size_t,uint16_t,uint16_t,ImagePixelFormat,uint8_t,jpeg_write_cb,void *);
  void retire();
 private:
  void *state_=nullptr;
  bool borrowed_=false,retiring_=false;
};
