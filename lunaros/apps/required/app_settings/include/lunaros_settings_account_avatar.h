#pragma once
#include "desktop/ui/wallpaper.h"
#include <memory>
#include <string>
#include <string_view>
namespace lunaredu::settings {
// Product projects authenticated account metadata; Settings owns this consumer.
struct AccountAvatarRequest {
  bool bound=false, available=false;
  std::string account, revision, url, format;
  int epoch=0;
  uint16_t width=0,height=0;
};
// One account per device. The immutable display image may remain cached; raw
// read scratch retires after completion, never while an async read is Pending.
// Called only by the UI observation owner.
std::shared_ptr<const desktop::DesktopImage> observe_account_avatar(const AccountAvatarRequest &,uint32_t now_ms);
struct AccountAvatarView {
  bool bound=false,available=false;
  std::string_view account,revision,url,format;
  int epoch=0;uint16_t width=0,height=0;
};
// Borrowed for this synchronous call only. Async descriptors own their strings.
std::shared_ptr<const desktop::DesktopImage> observe_account_avatar_view(const AccountAvatarView &,uint32_t now_ms);
}
