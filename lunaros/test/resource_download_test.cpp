#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "delivery/resource/resource_download.h"
#include "lunaredu_lunaros/network/http_client.h"
#include "lunaredu_lunaros/persistence/storage.h"

namespace {

using namespace lunaredu::lunaros;

struct FakeState {
  bool open = true;
  bool read_fails = false;
  bool write_fails = false;
  bool move_fails = false;
  bool capacity_available = true;
  size_t reported_length = 3;
  std::string body = "abc";
  std::string temporary_content;
  std::string installed_content;
  NetworkDownloadRequest request;
} g_fake;

class FakeReader final : public NetworkDownloadReader {
public:
  int Read(char *buffer, size_t buffer_size) override {
    if (g_fake.read_fails) {
      return -1;
    }
    if (offset_ >= g_fake.body.size()) {
      return 0;
    }
    const size_t size =
        std::min(buffer_size, g_fake.body.size() - offset_);
    std::memcpy(buffer, g_fake.body.data() + offset_, size);
    offset_ += size;
    if (g_fake.request.progress_callback) {
      g_fake.request.progress_callback(
          {offset_, g_fake.reported_length,
           static_cast<int>(offset_ * 100 / g_fake.reported_length), 0});
    }
    return static_cast<int>(size);
  }
  size_t content_length() const override { return g_fake.reported_length; }
  size_t total_read() const override { return offset_; }
  int GetLastError() const override { return 0; }
  void Close() override {}

private:
  size_t offset_ = 0;
};

void reset_fake() { g_fake = {}; g_fake.open = true; g_fake.reported_length = 3; g_fake.body = "abc"; }

ResourceDescriptor descriptor() {
  ResourceDescriptor value;
  value.id = "pet.face";
  value.revision = "v1";
  value.url = "/assets/face.bin";
  value.format = "rgb666-u8x3";
  value.width = 1;
  value.height = 1;
  value.maximum_bytes = 8;
  return value;
}

void expect_error(const internal::ResourceDownloadResult &result,
                  const char *error) {
  assert(!result.success);
  assert(result.error == error);
}

} // namespace

namespace lunaredu::lunaros {

std::unique_ptr<NetworkDownloadReader>
network_start_download(NetworkDownloadRequest request, NetworkDownloadFailure *) {
  g_fake.request = std::move(request);
  return g_fake.open ? std::make_unique<FakeReader>() : nullptr;
}

} // namespace lunaredu::lunaros

namespace lunaredu::lunaros::internal {

bool ensure_private_resource_capacity(size_t, std::string &error) {
  if (g_fake.capacity_available) return true;
  error = "resource_storage_full";
  return false;
}

} // namespace lunaredu::lunaros::internal

namespace lunaredu::lunaros {

bool storage_delete_file(const char *path) {
  const std::string value = path != nullptr ? path : "";
  if (value.size() >= 5 && value.compare(value.size() - 5, 5, ".part") == 0) {
    g_fake.temporary_content.clear();
  }
  return true;
}

bool storage_write_streamed_file(const char *, StorageWriteStreamProducer producer,
                                 void *producer_context,
                                 size_t *written_bytes, std::string *error) {
  if (g_fake.write_fails) {
    if (error != nullptr) {
      *error = "storage_write_failed";
    }
    return false;
  }
  const auto writer = [](const uint8_t *data, size_t size, void *) {
    g_fake.temporary_content.append(reinterpret_cast<const char *>(data), size);
    return true;
  };
  std::string producer_error;
  const bool success = producer(writer, nullptr, producer_context,
                                producer_error);
  if (written_bytes != nullptr) {
    *written_bytes = g_fake.temporary_content.size();
  }
  if (error != nullptr) {
    *error = producer_error;
  }
  return success;
}

bool storage_move_file(const char *, const char *, std::string *error) {
  if (g_fake.move_fails) {
    if (error != nullptr) {
      *error = "move_failed";
    }
    return false;
  }
  g_fake.installed_content = g_fake.temporary_content;
  return true;
}

} // namespace lunaredu::lunaros

int main() {
  using namespace lunaredu::lunaros;
  ResourceTransportConfig transport;

  reset_fake();
  expect_error(internal::download_resource_transaction(descriptor(), transport),
               "resource_url_invalid");

  transport.base_url = "https://tools.example.test";
  reset_fake();
  g_fake.capacity_available = false;
  ResourceDescriptor private_resource = descriptor();
  private_resource.scope = ResourceScope::AccountPrivate;
  expect_error(
      internal::download_resource_transaction(private_resource, transport,
          nullptr, nullptr, nullptr, nullptr, {1, "account-A"}),
      "resource_storage_full");

  reset_fake();
  g_fake.open = false;
  expect_error(internal::download_resource_transaction(descriptor(), transport),
               "download_open_failed");

  reset_fake();
  g_fake.reported_length = 0;
  expect_error(internal::download_resource_transaction(descriptor(), transport),
               "resource_size_rejected");

  reset_fake();
  g_fake.read_fails = true;
  expect_error(internal::download_resource_transaction(descriptor(), transport),
               "network_read_failed");

  reset_fake();
  ResourceDescriptor checksum = descriptor();
  checksum.sha256 = "wrong";
  expect_error(internal::download_resource_transaction(checksum, transport),
               "resource_checksum_mismatch");

  reset_fake();
  g_fake.move_fails = true;
  expect_error(internal::download_resource_transaction(descriptor(), transport),
               "move_failed");

  reset_fake();
  int progress = 0;
  const auto progress_callback = [](int value, const void *context) {
    *const_cast<int *>(static_cast<const int *>(context)) = value;
  };
  const auto success = internal::download_resource_transaction(
      descriptor(), transport, progress_callback, &progress);
  assert(success.success);
  assert(success.size_bytes == 3);
  assert(g_fake.installed_content == "abc");
  assert(g_fake.request.url == "https://tools.example.test/assets/face.bin");
  assert(progress == 100);
  assert(g_fake.request.timeout_ms == 5000);

  reset_fake();
  int cancellation_checks = 0;
  const auto cancellation_callback = [](const void *context) {
    auto *checks = const_cast<int *>(static_cast<const int *>(context));
    ++(*checks);
    return *checks >= 2;
  };
  const auto cancelled = internal::download_resource_transaction(
      descriptor(), transport, nullptr, nullptr, cancellation_callback,
      &cancellation_checks);
  expect_error(cancelled, "download_cancelled");
  assert(g_fake.installed_content.empty());
  assert(g_fake.temporary_content.empty());

  const std::string cache_path = internal::resource_cache_path(descriptor());
  // The downloader appends ".part" while writing.  Keep both the installed
  // and temporary paths within LittleFS's 64-byte path limit.
  assert(cache_path.size() <= 64);
  assert(cache_path.size() + 5 <= 64);

  reset_fake();
  g_fake.body = "a";
  expect_error(internal::download_resource_transaction(descriptor(), transport),
               "download_incomplete");
  assert(g_fake.installed_content.empty());
  reset_fake();
  g_fake.body = "abcd";
  expect_error(internal::download_resource_transaction(descriptor(), transport),
               "resource_too_large");
  assert(g_fake.installed_content.empty());
  reset_fake();
  g_fake.reported_length = 1;
  g_fake.body = "a";
  expect_error(internal::download_resource_transaction(descriptor(), transport),
               "resource_pixel_size_mismatch");
  assert(g_fake.installed_content.empty());

  const internal::ResourceCacheScope account_a{1, "account-A"};
  const internal::ResourceCacheScope account_b{1, "account-B"};
  const internal::ResourceCacheScope rebound_a{2, "account-A"};
  const auto a_path = internal::resource_cache_path(private_resource, account_a);
  const auto b_path = internal::resource_cache_path(private_resource, account_b);
  assert(a_path != b_path);
  assert(a_path != internal::resource_cache_path(private_resource, rebound_a));
  assert(a_path == internal::resource_cache_path(private_resource, account_a));
  assert(a_path.size() + 5 <= 64 && b_path.size() + 5 <= 64);
  assert(internal::resource_cache_path(private_resource).empty());
  assert(internal::resource_cache_path(descriptor(), account_a) == cache_path);
  reset_fake();
  assert(internal::download_resource_transaction(private_resource, transport,
      nullptr, nullptr, nullptr, nullptr, account_a).success);
  std::cout << "Resource transaction length, pixel size, scope isolation and path-budget checks passed" << std::endl;
  return 0;
}
