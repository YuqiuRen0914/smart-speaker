#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace lunaredu::lunaros {

inline constexpr size_t kCooperativeExecutorCapacity = 8;
inline constexpr size_t kCooperativeExecutorStepIdBytes = 32;

using CooperativeExecutorStep = void (*)(void *context);

struct CooperativeExecutorStepDefinition {
  const char *id = nullptr;
  CooperativeExecutorStep run = nullptr;
  void *context = nullptr;
};

// A fixed-capacity list of short, non-blocking steps owned by one existing
// task. Registration is boot-only: seal() makes the list immutable before the
// FreeRTOS task starts polling it. This class never creates a task or allocates
// worker capacity dynamically.
class CooperativeExecutor {
 public:
  bool register_step(const CooperativeExecutorStepDefinition &definition,
                     std::string &error);
  bool seal(std::string &error);
  bool run_once() const;

  size_t size() const { return size_; }
  bool sealed() const { return sealed_; }

 private:
  struct Entry {
    std::array<char, kCooperativeExecutorStepIdBytes> id{};
    CooperativeExecutorStep run = nullptr;
    void *context = nullptr;
  };

  std::array<Entry, kCooperativeExecutorCapacity> entries_{};
  size_t size_ = 0;
  bool sealed_ = false;
};

}  // namespace lunaredu::lunaros
