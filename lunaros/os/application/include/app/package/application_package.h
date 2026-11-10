#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
namespace lunaredu::lunaros::apps {
// Immutable, synchronous borrow. The caller retains the backing allocation
// until admission/install returns; no view escapes into an execution session.
class PackageBytes {
 public:
  PackageBytes(const uint8_t *data, size_t size) : data_(data), size_(size) {}
  PackageBytes(const std::vector<uint8_t> &bytes) : PackageBytes(bytes.data(), bytes.size()) {}
  const uint8_t *data() const { return data_; }
  size_t size() const { return size_; }
 private:
  const uint8_t *data_;
  size_t size_;
};
struct Application {
  std::string id, name, version, description, sha256, package_url;
  std::string backend = "wasm";
  uint32_t size = 0;
  uint16_t color = 0xfdcd;
  bool installed = false;
  bool private_storage = false;
  bool device_basic = false;
  int tick_hz = 0;
  std::string icon_resource;
  // Required selected artifact. A release binds the entry to the
  // canonical application source; download/install never fetch other variants.
  std::string artifact_id, entry_format, source_sha256, entry_sha256;
  uint32_t package_version = 2;
  // Detached signature over the complete package digest, persisted for reboot
  // and checked again before every native-code launch.
  std::string signer_id, package_signature;
};
}
