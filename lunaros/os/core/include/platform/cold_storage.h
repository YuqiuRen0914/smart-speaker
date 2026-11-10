#pragma once

// Zero-initialized, task-context-only state. Keep ISR/cache-off data, locks,
// task control blocks and DMA objects out of this section. No-PSRAM/recovery
// targets keep normal BSS placement; this is a storage capability, not a
// product preference or runtime feature switch.
#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#include <esp_attr.h>
#if defined(CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#define LUNAROS_COLD_BSS EXT_RAM_BSS_ATTR
#endif
#endif
#ifndef LUNAROS_COLD_BSS
#define LUNAROS_COLD_BSS
#endif
