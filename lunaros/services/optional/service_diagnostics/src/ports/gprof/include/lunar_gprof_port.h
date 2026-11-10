#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>
#include <esp_partition.h>
#ifdef __cplusplus
extern "C" {
#endif
bool lunar_gprof_enter(void);
void lunar_gprof_leave(void);
void lunar_gprof_overflow(void);
void lunar_gprof_set_active(bool active);
void lunar_gprof_set_deadline(uint32_t deadline_ms);
bool lunar_gprof_expired(void);
uint32_t lunar_gprof_inflight(void);
bool lunar_gprof_overflowed(void);
const esp_partition_t *lunar_gprof_store_begin(void);
esp_err_t lunar_gprof_store_write(size_t offset,const void *bytes,size_t size);
const uint8_t *lunar_gprof_store_bytes(size_t *size);
void lunar_gprof_store_release(void);
#ifdef __cplusplus
}
#endif
