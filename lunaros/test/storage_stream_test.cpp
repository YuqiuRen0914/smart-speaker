#include <cassert>
#include <cstdio>
#include <string>
#include <windows.h>
#include "persistence/storage/littlefs_store_internal.h"
#include "persistence/storage/littlefs_read.h"
#include "persistence/storage/littlefs_write.h"
#include "persistence/storage/littlefs_path.h"
#include "persistence/storage/littlefs_volume.h"
#include "persistence/storage/littlefs_list.h"

namespace lunaredu::lunaros {
size_t storage_pool_user_used() { return 0; }
size_t storage_pool_user_reserve() { return 512 * 1024; }
}

// Only mount/path/usage plumbing is faked. Read/write/stream/format and their
// locks are the firmware translation units, exercised with real files/threads.
LittlefsStoreState &littlefs_store_state() { static LittlefsStoreState state; return state; }
bool littlefs_internal_fail(std::string *error, const std::string &message) {
  littlefs_store_state().last_error = message;
  if (error) *error = message;
  return false;
}
bool littlefs_internal_fail(std::string *error, const char *message) {
  return littlefs_internal_fail(error, std::string(message));
}
std::string littlefs_internal_errno_error(const char *prefix) { return prefix; }
bool littlefs_internal_canonicalize_path(const char *path, std::string &out) {
  if (!path || path[0] != '/') return false;
  out = path;
  return out.find("..") == std::string::npos;
}
std::string littlefs_internal_normalize_path(const char *path) {
  std::string out;
  return littlefs_internal_canonicalize_path(path, out) ? out : "";
}
std::string littlefs_internal_host_path(const char *path) { return std::string(".") + path; }
bool littlefs_internal_ensure_directory_recursive(const char *path) {
  if (std::string(path) == kLittlefsTransactionsPath)
    CreateDirectoryA(littlefs_internal_host_path(path).c_str(), nullptr);
  return true;
}
bool littlefs_internal_refresh_usage() { return true; }

DWORD WINAPI concurrent_read(void *) {
  std::string value;
  return littlefs_store_read_text_file("/stable.txt", value) && value == "stable" ? 0 : 1;
}
void check_concurrent_read() {
  HANDLE thread = CreateThread(nullptr, 0, concurrent_read, nullptr, 0, nullptr);
  assert(thread);
  // A callback holding Storage deadlocks this independent reader.
  assert(WaitForSingleObject(thread, 1500) == WAIT_OBJECT_0);
  DWORD result = 1;
  assert(GetExitCodeThread(thread, &result) && result == 0);
  CloseHandle(thread);
}
bool produce(StorageFileChunkWriter writer, void *target, void *, std::string &) {
  check_concurrent_read();
  assert(!littlefs_store_format());
  assert(!littlefs_store_delete_file("/download.bin"));
  assert(!littlefs_store_write_text_file("/download.bin", "bad"));
  assert(!littlefs_store_move_file("/stable.txt", "/download.bin", nullptr));
  std::vector<uint8_t> partial;
  assert(!littlefs_store_read_file("/download.bin", partial));
  assert(littlefs_store_write_text_file("/other.txt", "other"));
  const uint8_t bytes[] = {0, 1, 2, 255};
  return writer(bytes, sizeof(bytes), target);
}
bool consume(const uint8_t *data, size_t size, void *context) {
  check_concurrent_read();
  assert(!littlefs_store_format());
  assert(!littlefs_store_delete_file("/download.bin"));
  assert(!littlefs_store_move_file("/download.bin", "/moved.bin", nullptr));
  assert(!littlefs_store_write_text_file("/download.bin", "bad"));
  auto &out = *static_cast<std::string *>(context);
  out.append(reinterpret_cast<const char *>(data), size);
  return true;
}
bool cancel_producer(StorageFileChunkWriter, void *, void *, std::string &error) {
  error = "cancelled";
  return false;
}
bool partial_failure(StorageFileChunkWriter writer, void *target, void *, std::string &error) {
  const uint8_t replacement[] = {1, 2, 3};
  assert(writer(replacement, sizeof(replacement), target));
  error = "network_disconnected";
  return false;
}
int main() {
  littlefs_store_state().ready = true;
  assert(littlefs_store_write_text_file("/stable.txt", "stable"));
  uint8_t slice[3] = {};
  assert(littlefs_store_read_range("/stable.txt", 1, slice, sizeof(slice)));
  assert(std::string(reinterpret_cast<char *>(slice), 3) == "tab");
  assert(!littlefs_store_read_range("/stable.txt", 5, slice, sizeof(slice)));
  assert(!littlefs_store_read_range("/stable.txt", SIZE_MAX, slice, sizeof(slice)));
  std::string error;
  size_t failed_bytes = 42;
  assert(!littlefs_store_write_streamed_file("/stable.txt", partial_failure, nullptr, &failed_bytes, &error));
  std::string preserved;
  assert(failed_bytes == 0 && littlefs_store_read_text_file("/stable.txt", preserved) && preserved == "stable");
  assert(!littlefs_store_write_streamed_file("/stable.txt", cancel_producer, nullptr, nullptr, &error));
  assert(littlefs_store_read_text_file("/stable.txt", preserved) && preserved == "stable");
  assert(littlefs_store_write_text_file("/stable.txt", "replacement"));
  assert(littlefs_store_read_text_file("/stable.txt", preserved) && preserved == "replacement");
  assert(littlefs_store_write_text_file("/stable.txt", "stable"));
  assert(!littlefs_store_write_text_file("/.lunaros-transactions/0", "forbidden"));
  FILE *orphan = fopen("./.lunaros-transactions/0", "wb");
  assert(orphan && fclose(orphan) == 0);
  assert(littlefs_internal_cleanup_writes());
  assert(fopen("./.lunaros-transactions/0", "rb") == nullptr);
  size_t written = 0;
  assert(littlefs_store_write_streamed_file("/download.bin", produce, nullptr, &written, &error));
  assert(written == 4);
  std::string content;
  assert(littlefs_store_stream_file("/download.bin", consume, &content, &error));
  assert(content == std::string("\0\1\2\xff", 4));
  std::vector<uint8_t> binary;
  assert(littlefs_store_read_file("/download.bin", binary) && binary.size() == 4);
  assert(littlefs_store_read_text_file("/download.bin", content) && content.size() == 4);
  assert(littlefs_store_write_text_file("/empty.txt", ""));
  assert(littlefs_store_read_text_file("/empty.txt", content) && content.empty());
  assert(!littlefs_store_write_streamed_file("/cancelled.bin", cancel_producer, nullptr, nullptr, &error));
  assert(error == "cancelled" && !littlefs_store_path_exists("/cancelled.bin"));
  assert(littlefs_store_move_file("/download.bin", "/moved.bin", &error));
  assert(littlefs_store_delete_file("/moved.bin"));
  assert(littlefs_store_format());
  CreateDirectoryA("walk", nullptr);
  CreateDirectoryA("walk/child", nullptr);
  assert(littlefs_store_write_text_file("/walk/parent.txt", "p"));
  assert(littlefs_store_write_text_file("/walk/child/child.txt", "c"));
  std::vector<std::string> visited;
  auto collect = [](const StorageListEntry &entry, void *context) {
    static_cast<std::vector<std::string> *>(context)->push_back(entry.path);
    return true;
  };
  assert(littlefs_store_list_entries_recursive("/walk", 0, collect, &visited));
  assert(visited.size() == 2);
  visited.clear();
  assert(littlefs_store_list_entries_recursive("/walk", 1, collect, &visited));
  assert(visited.size() == 3 && visited.back() == "/walk/child/child.txt");
  size_t stopped_after = 0;
  auto stop = [](const StorageListEntry &, void *context) {
    ++*static_cast<size_t *>(context);
    return false;
  };
  assert(littlefs_store_list_entries_recursive("/walk", 3, stop, &stopped_after));
  assert(stopped_after == 1);
  // Capacity exhaustion and ancestor protection must leave all slots intact.
  std::array<std::string, 9> paths;
  std::array<LittlefsStreamLease, 9> leases;
  for (size_t i = 0; i < paths.size(); ++i) paths[i] = "/dir/" + std::to_string(i);
  for (size_t i = 0; i < 8; ++i) assert(leases[i].acquire(paths[i], false, &error));
  assert(!leases[8].acquire(paths[8], false, &error));
  assert(littlefs_internal_stream_conflicts("/dir", true));
  std::puts("Storage callback isolation, file lifetime, cancellation and bounded lease tests passed");
}
