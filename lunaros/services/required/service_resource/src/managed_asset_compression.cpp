#include "managed_asset_compression.h"
#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif
#include <esp_heap_caps.h>
#include <atomic>
#if CONFIG_IDF_TARGET_ESP32S31 || CONFIG_IDF_TARGET_ESP32S3
#include <miniz.h>
#endif

namespace lunaredu::lunaros::internal {
#if CONFIG_IDF_TARGET_ESP32S31 || CONFIG_IDF_TARGET_ESP32S3
namespace {
std::atomic<unsigned> decoder_state{0};
tinfl_decompressor *cached_decoder=nullptr;
struct DecoderLoan {
  tinfl_decompressor *decoder=nullptr;bool shared=false;
  DecoderLoan(){
    unsigned expected=0;shared=decoder_state.compare_exchange_strong(expected,1);
    if(shared){
      if(!cached_decoder)cached_decoder=static_cast<tinfl_decompressor *>(heap_caps_malloc(sizeof(tinfl_decompressor),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
      decoder=cached_decoder;
    }else decoder=static_cast<tinfl_decompressor *>(heap_caps_malloc(sizeof(tinfl_decompressor),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  }
  ~DecoderLoan(){
    if(!shared){heap_caps_free(decoder);return;}
    unsigned expected=1;
    if(!decoder_state.compare_exchange_strong(expected,0)){
      heap_caps_free(cached_decoder);cached_decoder=nullptr;decoder_state.store(0);
    }
  }
};
}
#endif
void retire_managed_asset_decoder(){
#if CONFIG_IDF_TARGET_ESP32S31 || CONFIG_IDF_TARGET_ESP32S3
  unsigned expected=0;
  for(;;){
    if(decoder_state.compare_exchange_strong(expected,2)){
      if(expected==0){heap_caps_free(cached_decoder);cached_decoder=nullptr;decoder_state.store(0);}
      return;
    }
    if(expected==2)return;
  }
#endif
}
bool decode_managed_asset(uint8_t *&data, size_t &bytes, std::string &error) {
  if (!compressed_asset(data, bytes)) return true;
  size_t decoded = 0;
  if (!compressed_asset_header(data, bytes, decoded)) {
    error = "asset_verification_failed"; return false;
  }
#if CONFIG_IDF_TARGET_ESP32S31 || CONFIG_IDF_TARGET_ESP32S3
  auto *output = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      16, decoded, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  // The ROM decompressor state is too large for the caller's task stack.
  DecoderLoan loan;auto *state=loan.decoder;
  if (!output || !state) {
    heap_caps_free(output);retire_managed_asset_decoder();
    error = "asset_psram_unavailable"; return false;
  }
  tinfl_init(state);
  size_t input_bytes = bytes - 16, output_bytes = decoded;
  const auto status = tinfl_decompress(state, data + 16, &input_bytes,
      output, output, &output_bytes,
      TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  if (status != TINFL_STATUS_DONE || input_bytes != bytes - 16 || output_bytes != decoded) {
    heap_caps_free(output); error = "asset_verification_failed"; return false;
  }
  heap_caps_free(data); data = output; bytes = decoded;
  return true;
#else
  error = "asset_compression_unavailable"; return false;
#endif
}
}
