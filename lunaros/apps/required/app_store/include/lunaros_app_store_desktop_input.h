#pragma once
#include "lunaros_app_store_desktop_host.h"
#include "desktop/ui/logical_input.h"
#include <algorithm>

namespace lunaredu::lunaros::apps {
template<class Events>
PackageDesktopInput package_desktop_input(const desktop::LogicalInput &sample, const Events &events) {
  PackageDesktopInput input;
  input.guest.controls.fresh = sample.fresh;
  std::copy(sample.axes.begin(),sample.axes.end(),input.guest.controls.axes.begin());
  input.guest.controls.buttons = (sample.primary ? 1 : 0) | (sample.secondary ? 2 : 0);
  input.guest.controls.encoder = events.encoder;
  input.guest.controls.confirm = events.confirm;
  input.guest.touch = sample.touch;
  input.guest.now_ms = uint32_t(events.now_us / 1000);
  input.guest.reserved_contact = events.top_owned;
  input.guest.back = events.back;
  input.guest.command = events.command;
  input.now_us = events.now_us;
  input.x = sample.axes[0]; input.y = sample.axes[1];
  return input;
}
}
