#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <psa/crypto.h>

namespace lunaredu::lunaros::digest {
using Sha256Value = std::array<uint8_t, 32>;
// One caller borrows a synchronous stream from eight boot-lived slots. PSA
// may allocate backend state; failed destructor cleanup retains its slot for
// retry. This component owns no task or product policy.
class Sha256 {
 public:
  Sha256() = default;
  ~Sha256();
  Sha256(const Sha256 &) = delete;
  Sha256 &operator=(const Sha256 &) = delete;
  bool begin();
  bool update(const uint8_t *data, size_t size);
  bool finish(Sha256Value &output);
  bool abort();
  // Preserve backend failures across cleanup so callers can distinguish a
  // temporarily unavailable hash engine from a digest mismatch.
  int32_t last_status() const { return last_status_; }
 private:
  void *state_ = nullptr;
  bool failed_ = false;
  int32_t last_status_ = 0;
};
bool initialize();
// Retry detached hash retirement. Live callers retain their resources.
// New operations refuse admission while an earlier retirement still fails.
bool cleanup();
// Length and pointer validity are public; byte equality uses the official
// constant-time primitive. Empty buffers compare equal without dereferencing.
bool constant_time_equal(const void *, const void *, size_t);
bool sha256(const uint8_t *data, size_t size, Sha256Value &output);
std::string sha256_hex(const uint8_t *data, size_t size);
std::string hex(const uint8_t *data, size_t size);
// Strict lower-case is the package/resource contract; OTA explicitly allows upper-case.
enum class HexCase { Lowercase, Any };
bool decode_hex(std::string_view text, uint8_t *output, size_t size,
                HexCase casing = HexCase::Lowercase);
} // namespace lunaredu::lunaros::digest
