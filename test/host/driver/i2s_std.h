#pragma once
#include <esp_err.h>
using i2s_chan_handle_t=void *;
esp_err_t i2s_channel_disable(i2s_chan_handle_t);
esp_err_t i2s_del_channel(i2s_chan_handle_t);

using i2s_slot_mode_t=int;
