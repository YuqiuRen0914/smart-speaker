#pragma once

#include <cstdint>

#include "lunaredu_lunaros/desktop/core/local_navigation.h"

namespace lunaredu::desktop {

// Called synchronously from the LVGL owner context. Implementations must only
// copy into a bounded queue and return; business work runs later on the
// product/LunarOS owner executor.
using LunarOSUiIntentHandler = bool (*)(void *context, const char *type,
                                        const char *payload);
using LunarOSWifiCredentialHandler = bool (*)(void *context, const char *ssid,
                                               const char *password);

void set_lunaros_ui_intent_handler(LunarOSUiIntentHandler handler,
                                   void *context = nullptr);
void clear_lunaros_ui_intent_handler();
void set_lunaros_ui_wifi_credential_handler(
    LunarOSWifiCredentialHandler handler, void *context = nullptr);
void clear_lunaros_ui_wifi_credential_handler();

// Consumes the bounded local_navigation request on the LVGL owner.
void sync_lunaros_ui_page_request();




} // namespace lunaredu::desktop
