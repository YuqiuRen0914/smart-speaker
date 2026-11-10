#pragma once
#include <cstddef>
#include <cstring>
#include <esp_heap_caps.h>
#ifdef ESP_PLATFORM
#include <esp_memory_utils.h>
#endif

namespace lunaredu::lunaros::internal {
// Only valid inside synchronous httpd_resp_send + flush: the caller owns the
// body throughout. SDK-generated headers are copied; the body is never copied.
// A single vector write keeps headers and body in the same TCP submission.
class ResponseBatch {
public:
  struct HeaderStorage {
    char *data=nullptr;size_t bytes=0;bool borrowed=false;
    ~HeaderStorage(){release();}
    void release(){if(!borrowed){heap_caps_free(data);data=nullptr;bytes=0;}}
    HeaderStorage()=default;HeaderStorage(const HeaderStorage &)=delete;
  };
  static constexpr size_t capacity = 8192;
  // Keep at least 2 KiB for SDK/CORS headers on the HTTP batching path. Larger
  // bodies continue through the SDK's normal synchronous send path.
  static constexpr size_t body_limit = capacity - 2048;
  static constexpr size_t initial_header_capacity = 512;
  ResponseBatch(const char *body, size_t size, HeaderStorage *storage=nullptr)
      : body_(body), body_size_(size),
        failed_(size > capacity || (size != 0 && body == nullptr)) {
    if(storage&&!storage->borrowed){storage_=storage;storage_->borrowed=true;
      headers_=storage->data;header_capacity_=storage->bytes;storage->data=nullptr;storage->bytes=0;}
  }
  ~ResponseBatch() {
    if(headers_)std::memset(headers_,0,header_size_);
    bool external=true;
#ifdef ESP_PLATFORM
    external=!headers_||esp_ptr_external_ram(headers_);
#endif
    if(storage_){
      if(header_capacity_<=1024&&external){storage_->data=headers_;storage_->bytes=header_capacity_;headers_=nullptr;}
      storage_->borrowed=false;
    }
    heap_caps_free(headers_);
  }
  ResponseBatch(const ResponseBatch &) = delete;
  ResponseBatch &operator=(const ResponseBatch &) = delete;

  bool append(const char *data, size_t size) {
    if (failed_) return false;
    if (size == 0) return true;
    if (data == nullptr) return fail();
    if (body_received_ < body_size_ && data == body_ + body_received_) {
      if (size > body_size_ - body_received_) return fail();
      body_received_ += size;
      return true;
    }
    // SDK emits all headers before the body. Reject unexpected writes instead
    // of silently changing their order or borrowing a temporary SDK buffer.
    if (body_received_ != 0 || size > capacity - body_size_ - header_size_)
      return fail();
    if (!reserve_headers(header_size_ + size)) return fail();
    std::memcpy(headers_ + header_size_, data, size);
    header_size_ += size;
    return true;
  }

  template<class Sender> bool flush(Sender send) const {
    if (failed_ || body_received_ != body_size_) return false;
    size_t header_sent = 0, body_sent = 0;
    while (header_sent < header_size_ || body_sent < body_size_) {
      const size_t h = header_size_ - header_sent;
      const size_t b = body_size_ - body_sent;
      const int n = send(h ? headers_ + header_sent : nullptr, h,
                         b ? body_ + body_sent : nullptr, b);
      if (n <= 0 || static_cast<size_t>(n) > h + b) return false;
      const size_t consumed = static_cast<size_t>(n);
      header_sent += consumed < h ? consumed : h;
      if (consumed > h) body_sent += consumed - h;
    }
    return true;
  }
private:
  bool reserve_headers(size_t required) {
    if (required <= header_capacity_) return true;
    const size_t budget = capacity - body_size_;
    size_t wanted = header_capacity_ ? header_capacity_ * 2 : initial_header_capacity;
    if (wanted < required) wanted = required;
    if (wanted > budget) wanted = budget;
    // Keep the existing prefix until all preferred allocation candidates have
    // failed or the replacement is owned. Normal headers need only 512 bytes;
    // unusually large headers grow within the same total response bound.
    auto *replacement = static_cast<char *>(heap_caps_realloc_prefer(headers_, wanted, 2,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!replacement) return false;
    headers_ = replacement;
    header_capacity_ = wanted;
    return true;
  }
  bool fail() { failed_ = true; return false; }
  const char *body_;
  size_t body_size_;
  char *headers_ = nullptr;
  size_t header_size_ = 0, header_capacity_ = 0, body_received_ = 0;
  bool failed_;
  HeaderStorage *storage_=nullptr;
};
}
