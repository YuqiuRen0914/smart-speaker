#pragma once

#include <stdint.h>
#include <string>

namespace lunaredu::lunaros { class DeviceProjectionRegistry; }

namespace lunaredu::device {

struct TfCardStoreStatus {
  bool mounted = false;
  std::string mount_path = "/sdcard";
  uint64_t capacity_bytes = 0;
  std::string last_error;
};

bool setup_tfcard_store(bool mount_now = false);
bool declare_tfcard_store_projection(
    lunaredu::lunaros::DeviceProjectionRegistry &registry,
    std::string &error);
void tfcard_store_deinit();
bool tfcard_store_setup_done();
bool tfcard_store_mount();
bool tfcard_store_unmount();
bool tfcard_store_ready();
TfCardStoreStatus tfcard_store_status();
bool tfcard_resolve_path(const char *path, std::string &host_path);

} // namespace lunaredu::device
