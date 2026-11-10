#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef ESP_PLATFORM
#include <esp_rom_crc.h>
#else
#ifdef __cplusplus
extern "C" {
#endif
uint32_t esp_rom_crc32_le(uint32_t, const uint8_t *, uint32_t);
#ifdef __cplusplus
}
#endif
#endif
// IEEE CRC-32 with the SDK's incremental seed convention; empty input
// preserves seed. C consumers share the same official engine as C++ callers.
static inline uint32_t lunaros_crc32_update(uint32_t seed,const void *data,size_t size) {
  const uint8_t *bytes=(const uint8_t *)data;
  while(size){
    const uint32_t count=size>UINT32_MAX?UINT32_MAX:(uint32_t)size;
    seed=esp_rom_crc32_le(seed,bytes,count);
    bytes+=count;size-=count;
  }
  return seed;
}
