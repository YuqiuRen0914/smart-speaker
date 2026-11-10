#include "lunaredu_lunaros/delivery/managed_assets.h"
#include "lunaredu_lunaros/persistence/storage.h"
#include "lunaredu_lunaros/persistence/storage_pool.h"
#include "lunaredu_lunaros/network/network_jobs.h"
#include "lunaredu_lunaros/network/http_client.h"
#include "delivery/resource/managed_asset_formats.h"
#include <esp_heap_caps.h>
#include <psa/crypto.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace lunaredu::lunaros;
namespace {
std::map<std::string, std::string> files;
std::map<uint32_t, NetworkJobDefinition> jobs;
std::string network_data;
size_t content_length = 0;
bool fail_write = false, admit = true;
uint32_t next_job = 0;
void (*read_hook)() = nullptr;
std::string digest(const std::string &data) {
  uint8_t hash[32]; size_t size = 0;
  psa_hash_compute(PSA_ALG_SHA_256, reinterpret_cast<const uint8_t *>(data.data()), data.size(), hash, 32, &size);
  std::string result;
  for (auto ch : hash) { result += "0123456789abcdef"[ch >> 4]; result += "0123456789abcdef"[ch & 15]; }
  return result;
}
class Reader : public NetworkDownloadReader {
  size_t cursor = 0;
public:
  int Read(char *target, size_t size) override {
    if (read_hook) read_hook();
    size = std::min(size, network_data.size() - cursor);
    std::memcpy(target, network_data.data() + cursor, size); cursor += size; return static_cast<int>(size);
  }
  size_t content_length() const override { return ::content_length; }
  size_t total_read() const override { return cursor; }
  int GetLastError() const override { return 0; }
  void Close() override {}
};
NetworkJobRunResult dispatch() {
  assert(!jobs.empty());
  const auto [id, definition] = *jobs.begin();
  auto result = definition.runner({id}, definition.context);
  jobs.erase(id);
  return result;
}
#ifdef _WIN32
HANDLE download_entered, download_release, font_loaded;
void block_download() {
  SetEvent(download_entered);
  assert(WaitForSingleObject(download_release, 5000) == WAIT_OBJECT_0);
}
DWORD WINAPI download_on_thread(void *) { (void)dispatch(); return 0; }
DWORD WINAPI load_font_on_thread(void *) {
  size_t bytes = 0; std::string error;
  auto *data = load_managed_asset("fonts", bytes, error);
  assert(data && bytes == 64);
  heap_caps_free(data);
  SetEvent(font_loaded);
  return 0;
}
void test_independent_asset_reads(const ManagedAssetRequest &font) {
  auto model = font; model.id = "hand_det";
  std::string error;
  assert(request_managed_asset(model, error));
  download_entered = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  download_release = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  font_loaded = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  read_hook = block_download;
  auto download = CreateThread(nullptr, 0, download_on_thread, nullptr, 0, nullptr);
  assert(WaitForSingleObject(download_entered, 5000) == WAIT_OBJECT_0);
  auto loader = CreateThread(nullptr, 0, load_font_on_thread, nullptr, 0, nullptr);
  const bool loaded_before_download_completed =
      WaitForSingleObject(font_loaded, 1000) == WAIT_OBJECT_0;
  // Same-resource replacement remains rejected while a download owns it.
  assert(!request_managed_asset(model, error));
  SetEvent(download_release);
  assert(WaitForSingleObject(download, 5000) == WAIT_OBJECT_0);
  assert(WaitForSingleObject(loader, 5000) == WAIT_OBJECT_0);
  assert(loaded_before_download_completed);
  read_hook = nullptr;
  for (auto handle : {download, loader, download_entered, download_release, font_loaded}) CloseHandle(handle);
  assert(remove_managed_asset("hand_det", error));
}
#endif
}
namespace lunaredu::lunaros {
bool storage_pool_enabled() { return true; }
size_t storage_total_bytes() { return kStoragePoolBytes; }
size_t storage_used_bytes() { return 0; }
size_t storage_pool_user_used() { return 0; }
size_t storage_pool_user_reserve() { return kStoragePoolDefaultUserReserve; }
bool storage_pool_admit(size_t, bool managed, std::string &error) {
  assert(managed); if (!admit) error = "storage_pool_full"; return admit;
}
bool storage_read_text_file(const char *path, std::string &text) {
  auto it = files.find(path); if (it == files.end()) return false; text = it->second; return true;
}
bool storage_read_range(const char *path, size_t offset, uint8_t *data, size_t size) {
  const auto it = files.find(path);
  if (it == files.end() || offset > it->second.size() || size > it->second.size() - offset) return false;
  std::memcpy(data, it->second.data() + offset, size); return true;
}
bool storage_write_text_file(const char *path, const char *text) { files[path] = text; return true; }
bool storage_move_file(const char *from, const char *to, std::string *) {
  if (!files.count(from)) return false;
  files[to] = files[from]; files.erase(from); return true;
}
bool storage_path_exists(const char *path) { return files.count(path) != 0; }
bool storage_delete_file(const char *path) { return files.erase(path) != 0; }
bool storage_stream_file(const char *path, StorageFileChunkWriter writer, void *context, std::string *) {
  auto it = files.find(path); if (it == files.end()) return false;
  return writer(reinterpret_cast<const uint8_t *>(it->second.data()), it->second.size(), context);
}
bool storage_write_streamed_file(const char *path, StorageWriteStreamProducer producer,
                                void *context, size_t *, std::string *error) {
  files[path].clear();
  auto write = [](const uint8_t *data, size_t size, void *target) {
    static_cast<std::string *>(target)->append(reinterpret_cast<const char *>(data), size);
    return !fail_write;
  };
  const bool ok = producer(write, &files[path], context, *error);
  if (!ok) files.erase(path);
  return ok;
}
std::unique_ptr<NetworkDownloadReader> network_start_download(NetworkDownloadRequest request, NetworkDownloadFailure *) {
  assert(request.transport_security == NetworkTransportSecurity::RequireTls);
  return std::make_unique<Reader>();
}
bool submit_network_job(const NetworkJobDefinition &definition, NetworkJobHandle &handle, std::string &) {
  handle.id = ++next_job; jobs[handle.id] = definition; return true;
}
bool network_job_active(NetworkJobHandle handle) { return jobs.count(handle.id) != 0; }
bool network_job_cancellation_requested(NetworkJobHandle handle) { return !network_job_active(handle); }
bool cancel_network_job(NetworkJobHandle handle, std::string &) { return jobs.erase(handle.id) != 0; }
}
int main(int argc, char **argv) {
  // Optional real factory publications use the same runtime format validator.
  for (int i = 1; i + 1 < argc; i += 2) {
    std::ifstream file(argv[i + 1], std::ios::binary);
    assert(file.good());
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});
    assert(internal::managed_asset_format_valid(argv[i], data.data(), data.size()));
    auto read = [](size_t offset, uint8_t *out, size_t bytes, void *opaque) {
      const auto &source = *static_cast<std::vector<uint8_t> *>(opaque);
      if (offset > source.size() || bytes > source.size() - offset) return false;
      std::memcpy(out, source.data() + offset, bytes); return true;
    };
    assert(internal::managed_asset_file_valid(argv[i], data.size(), read, const_cast<std::vector<uint8_t> *>(&data)));
    assert(!internal::managed_asset_format_valid(argv[i], data.data(), 3));
  }
  assert(storage_pool_fits(0x3e0000, 0, 1024, 0, 512 * 1024));
  assert(!storage_pool_fits(0x3e0000, SIZE_MAX, 1024, 0, 0));
  assert(!storage_pool_fits(0x3e0000, 0, SIZE_MAX, 0, 0));
  assert(!storage_pool_fits(0x3e0000, 0x300000, 0x50000, 0, 512 * 1024));
  assert(storage_pool_managed_path("/managed/fonts.bin"));
  assert(!storage_pool_managed_path("/managed-user.txt"));
  network_data = std::string(64, '\0');
  network_data[1] = 1; network_data[5] = 1; // SFNT and one bounded table
  content_length = network_data.size();
  ManagedAssetRequest request{"fonts", "one", "https://assets.invalid/font", digest(network_data), content_length};
  std::string error;
  files["/user/important.txt"] = "keep me";
  auto catalog = managed_assets_snapshot();
  assert(catalog.assets[0].id == "fonts" && catalog.assets[0].state == ManagedAssetState::Absent);
  auto bad = request; bad.id = "../user/important";
  assert(!request_managed_asset(bad, error));
  bad = request; bad.url = "http://assets.invalid/font";
  assert(!request_managed_asset(bad, error));
  assert(request_managed_asset(request, error));
  assert(!managed_asset_ready("fonts"));
  catalog = managed_assets_snapshot();
  assert(catalog.assets[0].state == ManagedAssetState::Invalid && catalog.assets[0].downloading);
  assert(catalog.assets[0].bytes == content_length && catalog.assets[0].revision == "one");
  assert(!request_managed_asset(request, error)); // active context cannot be replaced
  assert(dispatch().succeeded);
  assert(managed_asset_ready("fonts"));
  catalog = managed_assets_snapshot();
  assert(catalog.assets[0].state == ManagedAssetState::Ready && !catalog.assets[0].downloading);
  size_t bytes = 0;
  void *data = load_managed_asset("fonts", bytes, error);
  assert(data && bytes == 64 && std::memcmp(data, network_data.data(), bytes) == 0);
  heap_caps_free(data);
#ifdef _WIN32
  test_independent_asset_reads(request);
#endif
  asset_allocation_fail = true;
  assert(!load_managed_asset("fonts", bytes, error) && managed_asset_ready("fonts"));
  asset_allocation_fail = false;
  request.revision = "two";
  assert(request_managed_asset(request, error));
  fail_write = true;
  assert(!dispatch().succeeded && !managed_asset_ready("fonts"));
  assert(!files.count("/managed/fonts.bin")); // accepted single-copy semantics
  fail_write = false;
  resume_managed_assets(); // boot recovers durable pending publication
  assert(dispatch().succeeded);
  files["/managed/fonts.bin"][1] = 2;
  assert(!load_managed_asset("fonts", bytes, error) && !managed_asset_ready("fonts"));
  resume_managed_assets();
  assert(dispatch().succeeded);
  request.sha256 = std::string(64, '0');
  assert(request_managed_asset(request, error));
  assert(!dispatch().succeeded && !managed_asset_ready("fonts"));
  request.sha256 = digest(network_data);
  admit = false;
  assert(request_managed_asset(request, error));
  assert(!dispatch().succeeded);
  assert(files["/user/important.txt"] == "keep me");
  admit = true;
  assert(remove_managed_asset("fonts", error));
  assert(!managed_asset_ready("fonts"));
  assert(managed_assets_snapshot().assets[0].state == ManagedAssetState::Absent);
  assert(files["/user/important.txt"] == "keep me");
  std::cout << "Managed assets: publication, interruption, reboot, corruption, quota and PSRAM regressions passed\n";
}
