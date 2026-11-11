// In-memory implementation of the public persistence boundary for host tests.
// Production serialization, validation, restore and rollback run unchanged.
#include "lunaredu_lunaros/persistence/storage.h"
#include "product_time_storage_test.h"
#include <string>

namespace test_storage {
std::string record;
bool ready = true;
bool fail_read = false;
bool fail_write = false;
size_t writes = 0;
std::function<void()> before_write;
void reset() { record.clear(); ready = true; fail_read = fail_write = false; writes = 0; before_write = {}; }
}
namespace lunaredu::lunaros {
bool storage_ready() { return test_storage::ready; }
bool storage_path_exists(const char *) { return !test_storage::record.empty(); }
bool storage_read_text_file(const char *, std::string &text) {
  if (!test_storage::ready || test_storage::fail_read) return false;
  text = test_storage::record;
  return true;
}
bool storage_stream_file(const char *, StorageFileChunkWriter writer, void *context, std::string *) {
  if (!test_storage::ready || test_storage::fail_read) return false;
  const auto &record = test_storage::record;
  return writer(reinterpret_cast<const uint8_t *>(record.data()), record.size(), context);
}
bool storage_write_text_file(const char *, const char *text) {
  ++test_storage::writes;
  if (test_storage::before_write) test_storage::before_write();
  if (!test_storage::ready || test_storage::fail_write) return false;
  test_storage::record = text;
  return true;
}
}
