#include "system/boot_guard.h"
#include "boot_guard_policy.h"
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <esp_rom_crc.h>
#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#include <esp_app_desc.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <esp_private/startup_internal.h>
#endif

namespace lunaredu::lunaros::boot_guard {
namespace {
uint32_t checksum(const Record &record) {
  // ROM is available before app_main; preserve the persisted IEEE CRC bytes.
  return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&record),
                        static_cast<uint32_t>(offsetof(Record, crc)));
}
}
void seal(Record &record) { record.crc = checksum(record); }
bool valid(const Record &record) {
  return record.magic == 0x3147424c && record.schema == 1 &&
         record.armed <= 1 && record.rescues <= 1 && record.crc == checksum(record);
}
void begin(Record &record, uint64_t image, bool abnormal) {
  if (!valid(record)) {
    std::memset(&record, 0, sizeof(record));
    record.magic = 0x3147424c; record.schema = 1;
  }
  if (abnormal && record.armed && record.image == image) {
    if (record.failures != std::numeric_limits<uint32_t>::max()) ++record.failures;
  } else {
    record.failures = 0;
    // A deliberately selected fresh image starts a new failure window, while
    // a failed rescue never acquires an unlimited alternating-slot budget.
    if (!abnormal && record.image == image) record.rescues = 0;
  }
  record.image = image; record.armed = 1; seal(record);
}
bool rescue_due(const Record &record) { return valid(record) && record.failures >= 3; }
bool admit_rescue(Record &record) {
  if (!rescue_due(record) || record.rescues) return false;
  record.rescues = 1; seal(record); return true;
}
void healthy(Record &record) { record.failures = record.rescues = 0; seal(record); }
}

namespace lunaredu::lunaros {
namespace {
#if defined(ESP_PLATFORM) && CONFIG_LUNAROS_BOOT_GUARD
RTC_NOINIT_ATTR boot_guard::Record retained;
uint32_t reset_reason = 0;
bool prepared = false, started = false, watchdog_ready = false;
std::string last_error;
esp_task_wdt_user_handle_t heartbeats[2] = {};
std::mutex guard_mutex;
int64_t healthy_since = -1;
bool normal_timeout = false, health_committed = false;
bool previous_health = true;
uint32_t health_interruptions = 0, last_blocked_mask = 0;
uint64_t last_interruption_ms = 0, interrupted_health_ms = 0;

// ESP reset-reason initialization is a C++ constructor. Read it only after
// constructors, through the SDK startup stage, and before app_main/configuration.
// Failures before this hook require protection in the factory bootloader.
void prepare_guard() {
  if (prepared) return;
  const auto reason = esp_reset_reason();
  reset_reason = static_cast<uint32_t>(reason);
  const bool abnormal = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
      reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT || reason == ESP_RST_CPU_LOCKUP;
  const auto *app = esp_app_get_description();
  uint64_t identity = 14695981039346656037ULL;
  for (uint8_t byte : app->app_elf_sha256) { identity ^= byte; identity *= 1099511628211ULL; }
  boot_guard::begin(retained, identity, abnormal);
  prepared = true;
}
ESP_SYSTEM_INIT_FN(lunaros_boot_guard, SECONDARY, BIT(0), 250) {
  prepare_guard();
  return ESP_OK;
}
esp_task_wdt_config_t watchdog_config(uint32_t timeout) {
  esp_task_wdt_config_t config{};
  config.timeout_ms = timeout; config.trigger_panic = true;
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
  config.idle_core_mask |= 1U;
#endif
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1 && !CONFIG_FREERTOS_UNICORE
  config.idle_core_mask |= 2U;
#endif
  return config;
}
#endif
}
bool system_boot_guard_start(BootGuardRescue rescue, void *context, std::string &error) {
  error.clear();
#if defined(ESP_PLATFORM) && CONFIG_LUNAROS_BOOT_GUARD
  prepare_guard();
  bool attempt = false;
  {
    std::lock_guard lock(guard_mutex);
    if (started) return true;
    attempt = boot_guard::admit_rescue(retained);
  }
  if (attempt && rescue) {
    if (rescue(error, context)) esp_restart();
    std::lock_guard lock(guard_mutex);
    last_error = error.empty() ? "boot_rescue_unavailable" : error;
    error.clear(); // Startup succeeds into the product's management-only mode.
  }
  std::lock_guard lock(guard_mutex);
  auto config = watchdog_config(60000);
  auto result = esp_task_wdt_reconfigure(&config);
  if (result == ESP_ERR_INVALID_STATE) result = esp_task_wdt_init(&config);
  if (result != ESP_OK) { error = "boot_watchdog_initialize_failed"; last_error = "boot_watchdog_initialize_failed"; return false; }
  result = heartbeats[0] ? ESP_OK : esp_task_wdt_add_user("lunaros.main", &heartbeats[0]);
  if (result != ESP_OK) { error = "boot_watchdog_subscribe_failed"; last_error = "boot_watchdog_subscribe_failed"; return false; }
  watchdog_ready = started = true;
#else
  (void)rescue; (void)context;
#endif
  return true;
}
void system_boot_guard_heartbeat(BootGuardOwner owner) {
#if defined(ESP_PLATFORM) && CONFIG_LUNAROS_BOOT_GUARD
  std::lock_guard lock(guard_mutex);
  if (!watchdog_ready) return;
  const unsigned index = owner == BootGuardOwner::Network ? 1 : 0;
  if (!heartbeats[index] && esp_task_wdt_add_user("lunaros.network", &heartbeats[index]) != ESP_OK) {
    last_error = "network_watchdog_subscribe_failed"; return;
  }
  if (esp_task_wdt_reset_user(heartbeats[index]) != ESP_OK) last_error = "boot_watchdog_heartbeat_failed";
#else
  (void)owner;
#endif
}
void system_boot_guard_observe_health(bool healthy_now, uint32_t blocked_mask) {
#if defined(ESP_PLATFORM) && CONFIG_LUNAROS_BOOT_GUARD
  std::lock_guard lock(guard_mutex);
  if (!started) return;
  const int64_t now = esp_timer_get_time() / 1000;
  if (!healthy_now) {
    if (previous_health) {
      ++health_interruptions;
      last_interruption_ms = static_cast<uint64_t>(now);
      interrupted_health_ms = healthy_since < 0 ? 0 : static_cast<uint64_t>(now - healthy_since);
      last_blocked_mask = blocked_mask;
    } else last_blocked_mask |= blocked_mask;
    previous_health = false;
    healthy_since = -1; health_committed = false; return;
  }
  previous_health = true;
  if (healthy_since < 0) healthy_since = now;
  if (now - healthy_since < 60000) return;
  if (!health_committed) { boot_guard::healthy(retained); health_committed = true; }
  if (!normal_timeout) {
    auto config = watchdog_config(15000);
    if (esp_task_wdt_reconfigure(&config) == ESP_OK) normal_timeout = true;
    else last_error = "boot_watchdog_reconfigure_failed";
  }
#else
  (void)healthy_now;
  (void)blocked_mask;
#endif
}
BootGuardSnapshot system_boot_guard_snapshot() {
  BootGuardSnapshot snapshot;
#if defined(ESP_PLATFORM) && CONFIG_LUNAROS_BOOT_GUARD
  std::lock_guard lock(guard_mutex);
  snapshot.enabled = true; snapshot.watchdog_active = watchdog_ready;
  snapshot.rescue_requested = boot_guard::rescue_due(retained);
  snapshot.consecutive_failures = retained.failures; snapshot.rescue_attempts = retained.rescues;
  snapshot.reset_reason = reset_reason; snapshot.error = last_error;
  snapshot.configured_timeout_ms = watchdog_ready ? (normal_timeout ? 15000 : 60000) : 0;
  snapshot.continuous_health_ms = healthy_since < 0 ? 0 :
      static_cast<uint64_t>(std::max<int64_t>(0,esp_timer_get_time()/1000-healthy_since));
  snapshot.health_committed = health_committed;
  snapshot.health_interruptions = health_interruptions;
  snapshot.last_blocked_mask = last_blocked_mask;
  snapshot.last_interruption_ms = last_interruption_ms;
  snapshot.interrupted_health_ms = interrupted_health_ms;
#endif
  return snapshot;
}
}
