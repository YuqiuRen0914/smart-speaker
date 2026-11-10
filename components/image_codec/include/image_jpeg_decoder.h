#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <memory>
#include <cstdlib>

// Own the decoder allocation, including its alignment-aware deleter. No second
// pixel buffer or platform codec headers are needed by consumers.
class ImageByteBuffer {
  std::unique_ptr<uint8_t, void (*)(void *)> bytes_{nullptr, std::free};
  size_t size_ = 0, capacity_=0;
public:
  ImageByteBuffer() = default;
  ImageByteBuffer(ImageByteBuffer &&other) noexcept
      : bytes_(std::move(other.bytes_)), size_(other.size_),capacity_(other.capacity_) { other.size_=other.capacity_=0; }
  ImageByteBuffer &operator=(ImageByteBuffer &&other) noexcept {
    if (this != &other) { bytes_ = std::move(other.bytes_); size_=other.size_;capacity_=other.capacity_;other.size_=other.capacity_=0; }
    return *this;
  }
  void adopt(uint8_t *bytes, size_t size, void (*release)(void *),size_t capacity=0) {
    bytes_ = decltype(bytes_)(bytes, release);size_=size;capacity_=capacity?capacity:size;
  }
  uint8_t *data() { return bytes_.get(); }
  const uint8_t *data() const { return bytes_.get(); }
  size_t size() const { return size_; }
  size_t capacity() const {return capacity_;}
  bool resize(size_t size){if(size>capacity_)return false;size_=size;return true;}
  bool empty() const { return size_ == 0; }
};

struct Rgb565ImageBuffer {
  ImageByteBuffer data;
  uint16_t width = 0;
  uint16_t height = 0;
};

// Output is little-endian RGB565; maximum decoded image size is 320 * 240 pixels.
bool decode_jpeg_to_rgb565(const uint8_t *jpeg_data, size_t jpeg_size,
                           Rgb565ImageBuffer &image, std::string &error);

// A single synchronous image owner may retain one decoder until it retires.
// No threads, result cache or pixel copies. Errors retire opaque SDK state.
class JpegDecoderWorkspace {
 public:
  ~JpegDecoderWorkspace(){retire();}
  JpegDecoderWorkspace()=default;
  JpegDecoderWorkspace(const JpegDecoderWorkspace &)=delete;
  JpegDecoderWorkspace &operator=(const JpegDecoderWorkspace &)=delete;
  bool decode(const uint8_t *,size_t,Rgb565ImageBuffer &,std::string &);
  void retire();
 private:
  void *decoder_=nullptr;
};
