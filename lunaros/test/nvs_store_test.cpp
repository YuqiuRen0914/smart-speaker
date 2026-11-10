#include "nvs.h"
#include "nvs_flash.h"
#include "persistence/settings/nvs_store.h"
#include <cassert>
#include <cstring>
#include <iostream>

namespace {
int open_error = ESP_OK, read_error = ESP_OK, write_error = ESP_OK, commit_error = ESP_OK;
int open_handles = 0, data_reads = 0, commits = 0;
size_t advertised_size = 0;
std::string stored;
bool fail_data_read = false;
}
esp_err_t nvs_open(const char *, nvs_open_mode_t, nvs_handle_t *handle) {
  if (open_error) return open_error;
  ++open_handles; *handle = 1; return ESP_OK;
}
void nvs_close(nvs_handle_t) { --open_handles; }
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *data, size_t *size) {
  if (read_error) return read_error;
  if (!data) { *size = advertised_size; return ESP_OK; }
  ++data_reads;
  if (fail_data_read) return ESP_FAIL;
  if (*size < stored.size()) return ESP_ERR_NVS_INVALID_LENGTH;
  std::memcpy(data, stored.data(), stored.size()); *size = stored.size(); return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *data, size_t *size) {
  return nvs_get_blob(handle, key, data, size);
}
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *data, size_t size) {
  if (write_error) return write_error;
  stored.assign(static_cast<const char *>(data), size); return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t) { ++commits; return commit_error; }
int main() {
  using namespace lunaredu::lunaros;
  std::string value = "stale";
  assert(nvs_store_read_blob("test", "key", 2048, value) == NvsReadResult::Failed && value.empty());
  for (int failure :
       {ESP_ERR_NVS_NO_FREE_PAGES, ESP_ERR_NVS_NEW_VERSION_FOUND}) {
    test_nvs_init_error = failure;
    assert(!setup_nvs_store() && test_nvs_erases == 0);
  }
  test_nvs_init_error = ESP_OK;
  assert(setup_nvs_store());
  for (int error : {ESP_ERR_NVS_NOT_FOUND, ESP_FAIL}) {
    open_error = error;
    const auto expected = error == ESP_ERR_NVS_NOT_FOUND ? NvsReadResult::Missing : NvsReadResult::Failed;
    assert(nvs_store_read_blob("test", "key", 2048, value) == expected);
    assert(nvs_store_read_string_bounded("test", "key", 2048, value) == expected);
  }
  open_error = ESP_OK;
  for (int error : {ESP_ERR_NVS_NOT_FOUND, ESP_FAIL}) {
    read_error = error;
    const auto expected = error == ESP_ERR_NVS_NOT_FOUND ? NvsReadResult::Missing : NvsReadResult::Failed;
    assert(nvs_store_read_blob("test", "key", 2048, value) == expected);
    assert(nvs_store_read_string_bounded("test", "key", 2048, value) == expected);
  }
  read_error = ESP_OK;
  for (size_t size : {size_t(0), size_t(4096), SIZE_MAX}) {
    advertised_size = size;
    assert(nvs_store_read_blob("test", "key", 2048, value) == NvsReadResult::Failed);
    assert(nvs_store_read_string_bounded("test", "key", 2048, value) == NvsReadResult::Failed);
  }
  assert(data_reads == 0 && open_handles == 0);
  stored = std::string("a\0b", 3); advertised_size = stored.size();
  assert(nvs_store_read_blob("test", "key", 3, value) == NvsReadResult::Found && value == stored);
  fail_data_read = true;
  assert(nvs_store_read_blob("test", "key", 3, value) == NvsReadResult::Failed && value.empty());
  fail_data_read = false;
  stored = std::string("text\0", 5); advertised_size = 8; // concurrent shrink is safe
  assert(nvs_store_read_string_bounded("test", "key", 8, value) == NvsReadResult::Found && value == "text");
  stored = "no terminator"; advertised_size = stored.size();
  assert(nvs_store_read_string_bounded("test", "key", 20, value) == NvsReadResult::Failed);
  write_error = ESP_FAIL;
  assert(!nvs_store_write_blob("test", "key", "record") && commits == 0);
  write_error = ESP_OK; commit_error = ESP_FAIL;
  assert(!nvs_store_write_blob("test", "key", "record") && commits == 1);
  commit_error = ESP_OK;
  assert(nvs_store_write_blob("test", "key", "record") && stored == "record");
  assert(open_handles == 0);
  std::cout << "NVS bounded reads, missing/error distinction, handle cleanup and write errors passed\n";
}
