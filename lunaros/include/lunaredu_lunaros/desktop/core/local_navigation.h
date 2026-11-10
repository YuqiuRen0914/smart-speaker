#pragma once
namespace lunaredu::desktop {
// Mirror control only. Browser-local navigation must stay in the browser.
// Latest request wins; no page allocation and no LVGL call on the producer.
bool request_lunaros_ui_page(const char *page_id);
} // namespace lunaredu::desktop
