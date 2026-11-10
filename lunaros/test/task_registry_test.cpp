#include "kernel/tasks/task_registry_internal.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void check(bool condition, const char *expression, int line) {
  if (!condition) {
    std::cerr << "CHECK failed at line " << line << ": " << expression
              << std::endl;
    std::exit(1);
  }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

lunaredu::lunaros::ManagedTaskDefinition task(
    const char *id, uint32_t stack_bytes = 1024,
    lunaredu::lunaros::ManagedTaskLifetime lifetime =
        lunaredu::lunaros::ManagedTaskLifetime::Resident,
    lunaredu::lunaros::ManagedTaskMemory memory =
        lunaredu::lunaros::ManagedTaskMemory::Internal) {
  return {
      .id = id,
      .owner_service_id = "test.service",
      .role = "test",
      .stack_bytes = stack_bytes,
      .priority = 2,
      .core_affinity = -1,
      .lifetime = lifetime,
      .memory = memory,
  };
}

void configure_registry(
    lunaredu::lunaros::ManagedTaskRegistry &registry,
    size_t active = 3, size_t transient = 1, size_t internal = 4096,
    size_t external = 8192, size_t resident = 0, size_t per_owner = 0) {
  std::string error;
  if (resident == 0) {
    resident = active;
  }
  if (per_owner == 0) {
    per_owner = active;
  }
  CHECK(registry.configure(
      {
          .maximum_active_tasks = active,
          .maximum_transient_tasks = transient,
          .maximum_internal_stack_bytes = internal,
          .maximum_external_stack_bytes = external,
          .maximum_resident_tasks = resident,
          .maximum_tasks_per_owner = per_owner,
      },
      error));
  CHECK(error.empty());
}

void test_admission_snapshot_and_release() {
  lunaredu::lunaros::ManagedTaskRegistry registry;
  configure_registry(registry);
  std::string error;
  const auto admission = registry.admit(task("worker"), error);
  CHECK(static_cast<bool>(admission));
  CHECK(registry.activate(admission, 42, error));

  const auto snapshot = registry.snapshot();
  CHECK(snapshot.active_tasks == 1);
  CHECK(snapshot.resident_tasks == 1);
  CHECK(snapshot.internal_stack_bytes == 1024);
  CHECK(std::string(snapshot.tasks[admission.slot].id.data()) == "worker");
  CHECK(std::string(snapshot.tasks[admission.slot].owner_service_id.data()) ==
        "test.service");
  CHECK(snapshot.tasks[admission.slot].native_handle == 42);

  CHECK(registry.release(admission));
  CHECK(registry.snapshot().active_tasks == 0);
}

void test_resident_and_owner_budgets() {
  using lunaredu::lunaros::ManagedTaskLifetime;
  lunaredu::lunaros::ManagedTaskRegistry registry;
  configure_registry(registry, 5, 2, 8192, 8192, 1, 2);
  std::string error;

  CHECK(static_cast<bool>(registry.admit(task("resident-one"), error)));
  CHECK(!registry.admit(task("resident-two"), error));
  CHECK(error == "managed_task_resident_budget_exhausted");

  CHECK(static_cast<bool>(registry.admit(
      task("feature-one", 1024, ManagedTaskLifetime::Feature), error)));
  CHECK(!registry.admit(task("feature-two", 1024,
                             ManagedTaskLifetime::Feature),
                        error));
  CHECK(error == "managed_task_owner_budget_exhausted");

  auto other_owner = task("feature-other", 1024,
                          ManagedTaskLifetime::Feature);
  other_owner.owner_service_id = "other.service";
  CHECK(static_cast<bool>(registry.admit(other_owner, error)));
  const auto snapshot = registry.snapshot();
  CHECK(snapshot.resident_tasks == 1);
  CHECK(snapshot.feature_tasks == 2);
}

void test_duplicate_and_active_budget() {
  lunaredu::lunaros::ManagedTaskRegistry registry;
  configure_registry(registry, 2, 1, 4096, 8192);
  std::string error;
  const auto first = registry.admit(task("one"), error);
  CHECK(static_cast<bool>(first));
  CHECK(!registry.admit(task("one"), error));
  CHECK(error == "managed_task_already_active");
  const auto second = registry.admit(task("two"), error);
  CHECK(static_cast<bool>(second));
  CHECK(!registry.admit(task("three"), error));
  CHECK(error == "managed_task_active_budget_exhausted");
}

void test_transient_budget() {
  lunaredu::lunaros::ManagedTaskRegistry registry;
  configure_registry(registry, 3, 1, 4096, 8192);
  std::string error;
  const auto first = registry.admit(
      task("temporary-one", 1024,
           lunaredu::lunaros::ManagedTaskLifetime::Transient),
      error);
  CHECK(static_cast<bool>(first));
  CHECK(!registry.admit(
      task("temporary-two", 1024,
           lunaredu::lunaros::ManagedTaskLifetime::Transient),
      error));
  CHECK(error == "managed_task_transient_budget_exhausted");
}

void test_stack_budgets_and_memory_retarget() {
  lunaredu::lunaros::ManagedTaskRegistry registry;
  configure_registry(registry, 4, 2, 2048, 4096);
  std::string error;
  const auto internal = registry.admit(task("internal", 2048), error);
  CHECK(static_cast<bool>(internal));
  CHECK(!registry.admit(task("internal-overflow", 1), error));
  CHECK(error == "managed_task_internal_stack_budget_exhausted");

  const auto external = registry.admit(
      task("external", 3072,
           lunaredu::lunaros::ManagedTaskLifetime::Feature,
           lunaredu::lunaros::ManagedTaskMemory::External),
      error);
  CHECK(static_cast<bool>(external));
  CHECK(!registry.retarget_memory(external,
                                  lunaredu::lunaros::ManagedTaskMemory::Internal,
                                  error));
  CHECK(error == "managed_task_internal_stack_budget_exhausted");
  CHECK(registry.release(internal));
  CHECK(!registry.retarget_memory(external,
                                  lunaredu::lunaros::ManagedTaskMemory::Internal,
                                  error));
  CHECK(error == "managed_task_internal_stack_budget_exhausted");
  CHECK(registry.release(external));
}

void test_invalid_metadata_and_configuration() {
  lunaredu::lunaros::ManagedTaskRegistry registry;
  std::string error;
  auto invalid_budget = lunaredu::lunaros::ManagedTaskBudget{};
  invalid_budget.maximum_active_tasks = 25;
  CHECK(!registry.configure(invalid_budget, error));
  CHECK(error == "managed_task_invalid_active_budget");
  configure_registry(registry, 2, 1, 2048, 2048, 2, 2);

  auto invalid = task(nullptr);
  CHECK(!registry.admit(invalid, error));
  CHECK(error == "managed_task_missing_id");
  invalid = task("valid");
  invalid.owner_service_id = nullptr;
  CHECK(!registry.admit(invalid, error));
  CHECK(error == "managed_task_missing_owner");
}

}  // namespace

int main() {
  test_admission_snapshot_and_release();
  test_duplicate_and_active_budget();
  test_transient_budget();
  test_resident_and_owner_budgets();
  test_stack_budgets_and_memory_retarget();
  test_invalid_metadata_and_configuration();
  std::cout << "6 LunarOS managed task policy checks passed" << std::endl;
  return 0;
}
