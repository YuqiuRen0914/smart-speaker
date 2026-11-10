#include "persistence/storage/littlefs_list.h"

#include <cstring>
#include <dirent.h>
#include <mutex>
#include <string>
#include <sys/stat.h>

#include "persistence/storage/littlefs_path.h"
#include "littlefs_store_internal.h"

namespace {

LittlefsStoreState &state = littlefs_store_state();

struct RecursiveListContext {
  StorageEntryVisitor visitor = nullptr;
  void *context = nullptr;
  uint8_t depth = 0;
  uint8_t max_depth = 0;
  bool stopped = false;
  bool failed = false;
};

bool recursive_collect_entry_visitor(const StorageListEntry &entry,
                                     void *context) {
  auto *recursive_context = static_cast<RecursiveListContext *>(context);
  if (recursive_context == nullptr) {
    return false;
  }
  if (recursive_context->visitor != nullptr &&
      !recursive_context->visitor(entry, recursive_context->context)) {
    recursive_context->stopped = true;
    return false;
  }
  return true;
}

bool list_entries_recursive_internal(const std::string &path, uint8_t depth,
                                     uint8_t max_depth,
                                     StorageEntryVisitor visitor,
                                     void *context, bool &stopped);

bool recursive_directory_visitor(const StorageListEntry &entry, void *context) {
  if (!entry.is_directory) return true;
  auto &walk = *static_cast<RecursiveListContext *>(context);
  walk.failed = !list_entries_recursive_internal(
      entry.path, walk.depth + 1, walk.max_depth, walk.visitor,
      walk.context, walk.stopped);
  return !walk.failed && !walk.stopped;
}

bool list_entries_recursive_internal(const std::string &path, uint8_t depth,
                                     uint8_t max_depth,
                                     StorageEntryVisitor visitor,
                                     void *context, bool &stopped) {
  RecursiveListContext recursive_context;
  recursive_context.visitor = visitor;
  recursive_context.context = context;
  if (!littlefs_store_list_entries(path.c_str(),
                                   recursive_collect_entry_visitor,
                                   &recursive_context)) {
    return false;
  }
  if (recursive_context.stopped) {
    stopped = true;
    return true;
  }
  if (depth >= max_depth) {
    return true;
  }

  // A second directory pass preserves parent-before-child visitation without
  // retaining every child path. Memory now depends on depth, not fan-out.
  recursive_context.depth = depth;
  recursive_context.max_depth = max_depth;
  const bool listed = littlefs_store_list_entries(
      path.c_str(), recursive_directory_visitor, &recursive_context);
  stopped = recursive_context.stopped;
  return listed && !recursive_context.failed;
}

} // namespace

bool littlefs_store_list_entries(const char *path,
                                 StorageEntryVisitor visitor,
                                 void *context) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  if (!state.ready || path == nullptr || path[0] != '/' ||
      !littlefs_store_is_safe_path(path)) {
    return false;
  }
  const std::string normalized = littlefs_internal_normalize_path(path);
  if (normalized.empty()) {
    return false;
  }
  const std::string host_path =
      littlefs_internal_host_path(normalized.c_str());
  DIR *dir = opendir(host_path.c_str());
  if (dir == nullptr) {
    return false;
  }

  struct dirent *entry = nullptr;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    const std::string item_path =
        normalized == "/" ? std::string("/") + entry->d_name
                          : normalized + "/" + entry->d_name;
    if (littlefs_internal_transaction_path(item_path)) continue;
    const std::string item_host_path =
        littlefs_internal_host_path(item_path.c_str());
    struct stat info;
    if (::stat(item_host_path.c_str(), &info) != 0) {
      continue;
    }

    if (visitor != nullptr) {
      StorageListEntry item;
      item.path = item_path;
      item.size_bytes = static_cast<size_t>(info.st_size);
      item.is_directory = S_ISDIR(info.st_mode);
      item.modified_ms = static_cast<uint64_t>(info.st_mtime) * 1000ULL;
      if (!visitor(item, context)) {
        break;
      }
    }
  }

  closedir(dir);
  return true;
}

bool littlefs_store_list_entries_recursive(const char *path, uint8_t max_depth,
                                           StorageEntryVisitor visitor,
                                           void *context) {
  std::lock_guard<std::recursive_mutex> lock(state.mutex);
  if (!state.ready || path == nullptr || path[0] != '/' ||
      !littlefs_store_is_safe_path(path)) {
    return false;
  }

  const std::string normalized = littlefs_internal_normalize_path(path);
  if (normalized.empty()) {
    return false;
  }
  bool stopped = false;
  return list_entries_recursive_internal(normalized, 0, max_depth, visitor,
                                         context, stopped);
}
