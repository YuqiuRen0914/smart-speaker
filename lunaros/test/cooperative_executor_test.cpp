#include "lunaredu_lunaros/kernel/cooperative_executor.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void check(bool condition, const char *expression, int line) {
  if (!condition) {
    std::cerr << "CHECK failed at line " << line << ": " << expression
              << std::endl;
    std::exit(1);
  }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

void append_step(void *context) {
  auto *values = static_cast<std::vector<int> *>(context);
  values->push_back(static_cast<int>(values->size() + 1));
}

void noop_step(void *) {}

void test_requires_valid_definition() {
  lunaredu::lunaros::CooperativeExecutor executor;
  std::string error;
  CHECK(!executor.register_step({}, error));
  CHECK(error == "cooperative_executor_missing_step_id");
  CHECK(!executor.register_step({"missing_callback", nullptr, nullptr}, error));
  CHECK(error == "cooperative_executor_missing_step_callback");
  CHECK(!executor.seal(error));
  CHECK(error == "cooperative_executor_empty");
  CHECK(!executor.run_once());
}

void test_runs_steps_in_registration_order() {
  lunaredu::lunaros::CooperativeExecutor executor;
  std::vector<int> values;
  std::string error;
  CHECK(executor.register_step({"first", append_step, &values}, error));
  CHECK(executor.register_step({"second", append_step, &values}, error));
  CHECK(executor.seal(error));
  CHECK(executor.sealed());
  CHECK(executor.run_once());
  CHECK(values.size() == 2);
  CHECK(values[0] == 1);
  CHECK(values[1] == 2);
}

void test_rejects_duplicates_and_registration_after_seal() {
  lunaredu::lunaros::CooperativeExecutor executor;
  std::string error;
  CHECK(executor.register_step({"network", noop_step, nullptr}, error));
  CHECK(!executor.register_step({"network", noop_step, nullptr}, error));
  CHECK(error == "cooperative_executor_duplicate_step");
  CHECK(executor.seal(error));
  CHECK(!executor.register_step({"late", noop_step, nullptr}, error));
  CHECK(error == "cooperative_executor_sealed");
}

void test_enforces_fixed_capacity() {
  lunaredu::lunaros::CooperativeExecutor executor;
  std::string error;
  std::string ids[lunaredu::lunaros::kCooperativeExecutorCapacity + 1];
  for (size_t index = 0;
       index < lunaredu::lunaros::kCooperativeExecutorCapacity; ++index) {
    ids[index] = "step_" + std::to_string(index);
    CHECK(executor.register_step({ids[index].c_str(), noop_step, nullptr},
                                 error));
  }
  ids[lunaredu::lunaros::kCooperativeExecutorCapacity] = "overflow";
  CHECK(!executor.register_step(
      {ids[lunaredu::lunaros::kCooperativeExecutorCapacity].c_str(),
       noop_step, nullptr},
      error));
  CHECK(error == "cooperative_executor_capacity_exhausted");
  CHECK(executor.size() == lunaredu::lunaros::kCooperativeExecutorCapacity);
}

}  // namespace

int main() {
  test_requires_valid_definition();
  test_runs_steps_in_registration_order();
  test_rejects_duplicates_and_registration_after_seal();
  test_enforces_fixed_capacity();
  std::cout << "4 LunarOS cooperative executor checks passed" << std::endl;
  return 0;
}
