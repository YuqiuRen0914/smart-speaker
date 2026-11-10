#include "lunaredu_lunaros/kernel/cooperative_executor.h"

#include <cstring>

namespace lunaredu::lunaros {

bool CooperativeExecutor::register_step(
    const CooperativeExecutorStepDefinition &definition, std::string &error) {
  error.clear();
  if (sealed_) {
    error = "cooperative_executor_sealed";
    return false;
  }
  if (definition.id == nullptr || definition.id[0] == '\0') {
    error = "cooperative_executor_missing_step_id";
    return false;
  }
  if (definition.run == nullptr) {
    error = "cooperative_executor_missing_step_callback";
    return false;
  }
  const size_t id_length = std::strlen(definition.id);
  if (id_length >= kCooperativeExecutorStepIdBytes) {
    error = "cooperative_executor_step_id_too_long";
    return false;
  }
  for (size_t index = 0; index < size_; ++index) {
    if (std::strcmp(entries_[index].id.data(), definition.id) == 0) {
      error = "cooperative_executor_duplicate_step";
      return false;
    }
  }
  if (size_ >= entries_.size()) {
    error = "cooperative_executor_capacity_exhausted";
    return false;
  }

  Entry &entry = entries_[size_++];
  std::memcpy(entry.id.data(), definition.id, id_length);
  entry.run = definition.run;
  entry.context = definition.context;
  return true;
}

bool CooperativeExecutor::seal(std::string &error) {
  error.clear();
  if (sealed_) {
    return true;
  }
  if (size_ == 0) {
    error = "cooperative_executor_empty";
    return false;
  }
  sealed_ = true;
  return true;
}

bool CooperativeExecutor::run_once() const {
  if (!sealed_) {
    return false;
  }
  for (size_t index = 0; index < size_; ++index) {
    entries_[index].run(entries_[index].context);
  }
  return true;
}

}  // namespace lunaredu::lunaros
