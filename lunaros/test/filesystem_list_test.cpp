#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <vector>
#include "device_api/projections/littlefs_operations.h"
#include "lunaredu_lunaros/persistence/storage.h"
#include "lunaredu_lunaros/persistence/storage_pool.h"

namespace {
std::vector<lunaredu::lunaros::StorageListEntry> entries;
std::map<std::string, std::string> arguments;
size_t visited = 0;
bool has_arg(const char *name, void *) { return arguments.count(name) != 0; }
std::string arg(const char *name, void *) { return arguments.at(name); }
class RejectAllocator : public ArduinoJson::Allocator {
public:
  void *allocate(size_t) override { return nullptr; }
  void deallocate(void *) override {}
  void *reallocate(void *, size_t) override { return nullptr; }
};
}
namespace lunaredu::lunaros {
bool storage_pool_enabled() { return false; }
// Listing must never request a write allocation.
bool storage_pool_admit(size_t, bool, std::string &) { std::abort(); }
std::string storage_normalize_path(const char *path) { return path; }
bool storage_is_safe_path(const char *path) { return path[0] == '/'; }
bool storage_path_exists(const char *) { return true; }
bool storage_list_entries(const char *, StorageEntryVisitor visitor, void *context) {
  visited = 0;
  for (const auto &entry : entries) { ++visited; if (!visitor(entry, context)) break; }
  return true;
}
bool storage_stream_file(const char *, StorageFileChunkWriter, void *, std::string *) { return false; }
bool storage_write_text_file(const char *, const char *) { return false; }
bool storage_write_streamed_file(const char *, StorageWriteStreamProducer, void *, size_t *, std::string *) { return false; }
bool storage_delete_file(const char *) { return false; }
bool storage_ensure_directory(const char *) { return false; }
bool storage_format() { return false; }
const char *storage_last_error() { return ""; }
const char *storage_mount_path() { return "/test"; }
size_t storage_total_bytes() { return 0; }
size_t storage_used_bytes() { return 0; }
}

int main() {
  using namespace lunaredu::lunaros;
  DeviceBulkRequest request;
  request.has_arg = has_arg;
  request.arg = arg;
  JsonDocument doc;
  auto list = [&]() {
    doc.clear();
    return handle_littlefs_bulk_list_request(request, doc.to<JsonObject>());
  };
  assert(list().ok && doc["items"].size() == 0);
  for (size_t i = 0; i < 100; ++i)
    entries.push_back({"/item-" + std::to_string(i), i, false, 0});
  auto result = list();
  assert(!result.ok && result.status_code == 413 && doc.size() == 0);
  assert(visited <= 65);
  arguments["limit"] = "20";
  std::set<std::string> seen;
  do {
    assert(list().ok && doc["items"].size() <= 20 && measureJson(doc) <= 8192);
    for (JsonObjectConst entry : doc["items"].as<JsonArrayConst>())
      assert(seen.insert(entry["path"].as<std::string>()).second);
    if (!(doc["has_more"] | false)) break;
    arguments["offset"] = doc["next_offset"].as<std::string>();
  } while (true);
  assert(seen.size() == entries.size());
  for (const char *invalid : {"0", "65", "-1", "1x", "", "9999999999999999999999999"}) {
    arguments["limit"] = invalid;
    assert(list().status_code == 400);
  }
  arguments = {{"limit", "64"}, {"offset", "-1"}};
  assert(list().status_code == 400);
  arguments = {{"limit", "64"}};
  entries.assign(100, {"/" + std::string(200, '\x01'), 123, false, 123});
  assert(list().ok && doc["has_more"].as<bool>());
  assert(doc["items"].size() > 0 && doc["items"].size() < 64 && measureJson(doc) <= 8192);
  entries.resize(1);
  entries[0].path.assign(256, 'x');
  entries[0].path[0] = '/';
  assert(list().status_code == 413);
  entries[0].path = "/small";
  RejectAllocator allocator;
  JsonDocument no_memory(&allocator);
  assert(handle_littlefs_bulk_list_request(request, no_memory.to<JsonObject>()).status_code == 503);
  arguments.clear();
  assert(list().ok && doc["items"].size() == 1 && doc["has_more"].isNull());
  std::cout << "Filesystem list byte/item bounds, paging, legacy failure and OOM checks passed\n";
}
