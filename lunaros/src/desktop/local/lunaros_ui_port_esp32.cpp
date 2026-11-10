#include "desktop/local/local_shell_internal.h"
#include "desktop/core/local_navigation_internal.h"
#include "lunaredu_lunaros/desktop/local/application_view.h"
#include "lunaredu_lunaros/desktop/local/view_widgets.h"
#include "lunaredu_lunaros/desktop/local/local_view_port.h"

#include <algorithm>
#include <array>
#include <atomic>
#include "lunaredu_lunaros/desktop/core/local_navigation.h"
#include <mutex>
#include <string>

#include <esp_log.h>

#include "lunaredu_lunaros/desktop/local/lunaros_ui_bridge.h"
#include "lunaredu_lunaros/desktop/local/local_display.h"
#include "lunaredu_lunaros/desktop/core/page_state.h"

#include "lunaredu_lunaros/desktop/local/local_shell.h"

namespace {
constexpr const char *kTag = "lunaros_ui_port";

std::atomic<lunaredu::desktop::LunarOSUiIntentHandler> g_intent_handler{
    nullptr};
std::atomic<void *> g_intent_context{nullptr};
std::atomic<lunaredu::desktop::LunarOSWifiCredentialHandler>
    g_wifi_credential_handler{nullptr};
std::atomic<void *> g_wifi_credential_context{nullptr};
}

extern "C" void lunaros_ui_port_emit_intent(const char *type,
                                             const char *payload) {
  const auto handler = g_intent_handler.load(std::memory_order_acquire);
  if (handler == nullptr) {
    ESP_LOGW(kTag, "intent rejected: handler unavailable type=%s",
             type == nullptr ? "" : type);
    return;
  }
  void *context = g_intent_context.load(std::memory_order_relaxed);
  if (!handler(context, type == nullptr ? "" : type,
               payload == nullptr ? "{}" : payload)) {
    ESP_LOGW(kTag, "intent rejected: queue unavailable type=%s",
             type == nullptr ? "" : type);
  }
}

extern "C" int lunaros_ui_port_read_touch_raw(uint16_t *raw_x,
                                                uint16_t *raw_y) {
  return lunaredu::desktop::read_local_touch_raw(raw_x, raw_y) ? 1 : 0;
}

extern "C" int lunaros_ui_port_submit_wifi_credentials(
    const char *ssid, const char *password) {
  const auto handler =
      g_wifi_credential_handler.load(std::memory_order_acquire);
  if (handler == nullptr || ssid == nullptr || password == nullptr) {
    ESP_LOGW(kTag, "Wi-Fi credential rejected: local handler unavailable");
    return 0;
  }
  void *context = g_wifi_credential_context.load(std::memory_order_relaxed);
  if (!handler(context, ssid, password)) {
    ESP_LOGW(kTag, "Wi-Fi credential rejected: local mailbox unavailable");
    return 0;
  }
  return 1;
}

extern "C" void lunaros_ui_port_screen_changed(const char *page) {
  lunaredu::desktop::publish_presentation_page(page);
  ESP_LOGD(kTag, "screen=%s", page == nullptr ? "" : page);
}

extern "C" void lunaros_ui_port_ready(void) {
  ESP_LOGI(kTag, "shared LunarOS UI ready");
}

namespace {

void consume_remote_page_request() {
  lunaredu::desktop::LocalPageRequest request;
  if (lunaredu::desktop::take_local_page_request(request)) {
    lunaredu::desktop::request_local_shell_page(request.page_id.data(), request.revision);
  }
}

} // namespace

namespace lunaredu::desktop {

void set_lunaros_ui_intent_handler(LunarOSUiIntentHandler handler,
                                   void *context) {
  g_intent_context.store(context, std::memory_order_relaxed);
  g_intent_handler.store(handler, std::memory_order_release);
}

void clear_lunaros_ui_intent_handler() {
  g_intent_handler.store(nullptr, std::memory_order_release);
  g_intent_context.store(nullptr, std::memory_order_relaxed);
}

void set_lunaros_ui_wifi_credential_handler(
    LunarOSWifiCredentialHandler handler, void *context) {
  g_wifi_credential_context.store(context, std::memory_order_relaxed);
  g_wifi_credential_handler.store(handler, std::memory_order_release);
}

void clear_lunaros_ui_wifi_credential_handler() {
  g_wifi_credential_handler.store(nullptr, std::memory_order_release);
  g_wifi_credential_context.store(nullptr, std::memory_order_relaxed);
}

void sync_lunaros_ui_page_request() { consume_remote_page_request(); }



} // namespace lunaredu::desktop
