#include "delivery/resource/resource_eviction_batch.h"
#include <cassert>
#include <string>

int main() {
  using namespace lunaredu::lunaros;
  using namespace lunaredu::lunaros::internal;
  ResourceEvictionBatch batch;
  for (size_t n = 10000; n > 0; --n) {
    batch.offer({"/cache/" + std::to_string(n), n, false, n});
    assert(batch.count <= ResourceEvictionBatch::kCapacity);
  }
  batch.offer({"/directory", 0, true, 0});
  batch.sort();
  assert(batch.count == 32);
  for (size_t n = 0; n < batch.count; ++n)
    assert(batch.entries[n].modified_ms == n + 1);
  ResourceEvictionBatch tied;
  tied.offer({"/b", 1, false, 5});
  tied.offer({"/a", 1, false, 5});
  tied.sort();
  assert(tied.entries[0].path == "/a" && tied.entries[1].path == "/b");
}
