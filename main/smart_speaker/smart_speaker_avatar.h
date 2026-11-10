#pragma once
#include "lunaredu_account_resources.h"
#include "lunaredu_lunaros/device_link/device_link.h"
#include <lvgl.h>
namespace smart_speaker {
// Called on the display owner at most twice per second. Resource downloads
// are scheduled by LunarOS on its existing network I/O executor.
void update_account_avatar(const lunaredu::account_resources::AccountResourcesSnapshot &account);
const lv_image_dsc_t *account_avatar_image();
uint32_t account_avatar_generation();
}
