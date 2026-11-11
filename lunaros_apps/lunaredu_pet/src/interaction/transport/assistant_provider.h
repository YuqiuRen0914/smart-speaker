#pragma once
// Firmware selects exactly one protocol. Host LunarLink fixtures retain their profile.
#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#if defined(CONFIG_LUNAREDU_ASSISTANT_LUNARLINK)
#define ASSISTANT_USE_LUNARLINK 1
#else
#define ASSISTANT_USE_LUNARLINK 0
#endif
#elif !defined(ASSISTANT_USE_LUNARLINK)
#define ASSISTANT_USE_LUNARLINK 1
#endif
inline constexpr bool kAssistantUsesLunarLink = ASSISTANT_USE_LUNARLINK != 0;
