#include "kernel/execution/service_executor_internal.h"
#include "model/operation_context.h"

#include <cstring>
#include <span>
#include <new>
#include <utility>
#include <algorithm>

#ifdef ESP_PLATFORM
#include "kernel/task_signal.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

namespace lunaredu::lunaros {
struct ServiceCall::State {
  using Phase=ServiceCallPhase;
  static constexpr Phase Queued=Phase::Queued,Running=Phase::Running,
    CancelRequested=Phase::CancellationRequested,Complete=Phase::Completed;
  std::atomic<unsigned> references{1};
  std::atomic<Phase> phase{Queued};
  std::unique_ptr<ServiceRequest> request;
  ServiceOperationResult result;
  OperationContext execution=capture_operation_context();
  std::string work_error;
  uintptr_t owner = 0;
  const ServiceExecutorRegistry *registry = nullptr;
  ServiceExecutorState *executor = nullptr;
  void retain() { references.fetch_add(1, std::memory_order_relaxed); }
  void release() { if (references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this; }
  void execute() {
    Phase expected = Queued;
    if (phase.compare_exchange_strong(expected, Running, std::memory_order_acq_rel)) {
      OperationContextScope context(execution,false);
      request->execute(result);
      if (result.work_error_code) {
        work_error = result.work_error_code;
        result.work_error_code = work_error.c_str();
      }
    } else {
      result.ok = false; result.error = "service_executor_cancelled_before_start";
    }
    phase.store(Complete, std::memory_order_release);
  }
};
ServiceCall::~ServiceCall() { reset(); }
ServiceCall::ServiceCall(ServiceCall &&other) noexcept : state_(std::exchange(other.state_, nullptr)) {}
ServiceCall &ServiceCall::operator=(ServiceCall &&other) noexcept {
  if (this != &other) { reset(); state_ = std::exchange(other.state_, nullptr); }
  return *this;
}
void ServiceCall::reset() { if (auto *state = std::exchange(state_, nullptr)) state->release(); }
bool ServiceCall::ready() const {
  return phase()==ServiceCallPhase::Completed;
}
ServiceCallPhase ServiceCall::phase() const {
  return state_?state_->phase.load(std::memory_order_acquire):ServiceCallPhase::Invalid;
}
bool ServiceCall::cancel_before_start() {
  if (!state_) return false;
  auto expected = State::Queued;
  return state_->phase.compare_exchange_strong(expected, State::CancelRequested, std::memory_order_acq_rel)||expected==State::CancelRequested;
}
const ServiceOperationResult *ServiceCall::result() const { return ready() ? &state_->result : nullptr; }
const ServiceRequest *ServiceCall::request() const { return ready() ? state_->request.get() : nullptr; }
namespace {
#ifdef ESP_PLATFORM

struct ServiceInvocation {
  ServiceOperation operation = nullptr;
  void *context = nullptr;
  ServiceOperationResult *result = nullptr;
  SemaphoreHandle_t completion = nullptr;
  ServiceCall::State *owned = nullptr;
  const OperationContext *execution=nullptr; // Synchronous caller retains this frame until completion.
};

// Publish the caller's synchronous wait among registered owners, then reject a
// path back to it. All atomics use sequential consistency so concurrent A->B
// and B->A admissions cannot both miss the other's published edge.
class OwnerWait {
public:
  OwnerWait(RegistrationTable<ServiceExecutorState> &states, size_t count, uintptr_t caller,
            uintptr_t target) : states_(states), count_(count), caller_(caller) {
    for (size_t i = 0; i < count_; ++i)
      if (states_[i].task_handle.load() == caller_ && caller_) {
        states_[i].waiting_for.store(target);
        published_=true;
      }
  }
  ~OwnerWait() {
    if(!published_)return;
    for (size_t i = 0; i < count_; ++i)
      if (states_[i].task_handle.load() == caller_ && caller_)
        states_[i].waiting_for.store(0);
  }
  bool cyclic(uintptr_t target) const {
    if(!published_)return false;
    for (size_t step = 0; target && step < count_; ++step) {
      if (target == caller_) return true;
      uintptr_t next = 0;
      for (size_t i = 0; i < count_; ++i)
        if (states_[i].task_handle.load() == target) {
          next = states_[i].waiting_for.load();
          if (next) break;
        }
      target = next;
    }
    return false;
  }
private:
  RegistrationTable<ServiceExecutorState> &states_;
  size_t count_;
  uintptr_t caller_;
  bool published_=false;
};

void execute(ServiceInvocation &invocation) {
  if (invocation.owned) {
    invocation.owned->execute();
    // Completion publication alone must not release an executing request.
    invocation.owned->executor->pending_calls.fetch_sub(1, std::memory_order_release);
    invocation.owned->release();
    return;
  }
  if (invocation.result == nullptr) {
    return;
  }
  invocation.result->ok = true;
  invocation.result->error.clear();
  if (invocation.operation == nullptr) {
    invocation.result->ok = false;
    invocation.result->error = "invalid_service_operation";
    return;
  }
  OperationContextScope context(invocation.execution?*invocation.execution:OperationContext{},false);
  invocation.operation(invocation.context, *invocation.result);
}

#endif
void set_error(std::string *error, const char *message) {
  if (error) *error = message;
}
} // namespace

ServiceExecutorState *ServiceExecutorRegistry::find(const char *id) {
  if (id == nullptr || id[0] == '\0') return nullptr;
  for (size_t index = 0; index < executor_count_; ++index) {
    if (std::strcmp(executors_[index].id, id) == 0) return &executors_[index];
  }
  return nullptr;
}

const ServiceExecutorState *ServiceExecutorRegistry::find(const char *id) const {
  if (id == nullptr || id[0] == '\0') return nullptr;
  for (size_t index = 0; index < executor_count_; ++index) {
    if (std::strcmp(executors_[index].id, id) == 0) return &executors_[index];
  }
  return nullptr;
}

bool ServiceExecutorRegistry::setup_range(
    const ServiceExecutorDefinition *items, size_t count,
    std::string *error) {
  if (error) error->clear();
  if (!items || !count) {
    set_error(error, "invalid_service_executor_count");
    return false;
  }
  const std::span<const ServiceExecutorDefinition> definitions{items,count};
  // Validate the complete declaration before allocating or publishing anything.
  for (auto current = definitions.begin(); current != definitions.end(); ++current) {
    if (!current->id || !*current->id || !current->owner_service_id ||
        !*current->owner_service_id || !current->queue_depth ||
        std::strlen(current->id) >= sizeof(executors_[0].id) ||
        std::strlen(current->owner_service_id) >= sizeof(executors_[0].owner_service_id) ||
        (!current->activation.request && current->activation.context)) {
      set_error(error, "invalid_service_executor_definition");
      return false;
    }
    for (auto previous = definitions.begin(); previous != current; ++previous) {
      if (std::strcmp(previous->id, current->id) == 0) {
        set_error(error, "duplicate_service_executor");
        return false;
      }
    }
  }
  if (executor_count_) {
    bool same = executor_count_ == definitions.size();
    for (const auto &definition : definitions) {
      auto *existing = find(definition.id);
      same = same && existing && existing->queue_depth == definition.queue_depth &&
          std::strcmp(existing->owner_service_id, definition.owner_service_id) == 0 &&
          existing->activation.request == definition.activation.request &&
          existing->activation.context == definition.activation.context;
    }
    if (!same) set_error(error, "service_executor_registry_already_configured");
    return same;
  }
  for (const auto &definition : definitions) {
    if (!register_executor(definition, error)) {
      for (size_t index = 0; index < executor_count_; ++index) {
#ifdef ESP_PLATFORM
        vQueueDelete(executors_[index].queue);
#endif
        executors_[index].queue = nullptr;
        executors_[index].id[0] = '\0';
      }
      executors_.clear();
      executor_count_ = 0;
      return false;
    }
  }
  return true;
}

bool ServiceExecutorRegistry::can_reset(std::string *error) const {
  if (error != nullptr) error->clear();
  for (size_t index = 0; index < executor_count_; ++index) {
    const ServiceExecutorState &executor = executors_[index];
    bool busy = executor.task_handle.load(std::memory_order_acquire) != 0 ||
        executor.pending_calls.load(std::memory_order_acquire) != 0;
#ifdef ESP_PLATFORM
    busy = busy || (executor.queue != nullptr &&
                    uxQueueMessagesWaiting(executor.queue) != 0);
#endif
    if (busy) {
      set_error(error, "service_executor_not_quiescent");
      return false;
    }
  }
  return true;
}

bool ServiceExecutorRegistry::reset(std::string *error) {
  if (!can_reset(error)) return false;
  for (size_t index = 0; index < executor_count_; ++index) {
#ifdef ESP_PLATFORM
    if (executors_[index].queue != nullptr) {
      vQueueDelete(executors_[index].queue);
    }
#endif
    executors_[index].id[0] = '\0';
    executors_[index].owner_service_id[0] = '\0';
    executors_[index].queue = nullptr;
    executors_[index].task_handle.store(0, std::memory_order_release);
    executors_[index].queue_depth = 0;
    executors_[index].activation = {};
    executors_[index].retiring.store(false, std::memory_order_release);
  }
  executors_.clear();
  executor_count_ = 0;
  return true;
}

bool ServiceExecutorRegistry::register_executor(
    const ServiceExecutorDefinition &definition, std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  if (definition.id == nullptr || definition.id[0] == '\0' ||
      definition.owner_service_id == nullptr ||
      definition.owner_service_id[0] == '\0' || definition.queue_depth == 0 ||
      (!definition.activation.request && definition.activation.context)) {
    set_error(error, "invalid_service_executor");
    return false;
  }
  if (std::strlen(definition.id) >= sizeof(executors_[0].id) ||
      std::strlen(definition.owner_service_id) >=
          sizeof(executors_[0].owner_service_id)) {
    set_error(error, "service_executor_metadata_too_long");
    return false;
  }
  if (find(definition.id) != nullptr) {
    set_error(error, "service_executor_already_registered");
    return false;
  }
  auto *stored = executors_.append();
  if (!stored) { set_error(error, "service_executor_storage_unavailable"); return false; }

#ifdef ESP_PLATFORM
  QueueHandle_t queue =
      xQueueCreate(definition.queue_depth, sizeof(ServiceInvocation));
  if (queue == nullptr) {
    executors_.pop_back();
    set_error(error, "service_executor_queue_allocation_failed");
    return false;
  }

#else
  QueueHandle_t queue = nullptr;
#endif
  ServiceExecutorState &executor = *stored;
  ++executor_count_;
  std::memcpy(executor.id, definition.id, std::strlen(definition.id) + 1);
  std::memcpy(executor.owner_service_id, definition.owner_service_id,
              std::strlen(definition.owner_service_id) + 1);
  executor.queue = queue;
  executor.queue_depth = definition.queue_depth;
  executor.activation = definition.activation;
  return true;
}

size_t ServiceExecutorRegistry::snapshot(ServiceExecutorSnapshot *output,
                                         size_t capacity) const {
  if (output == nullptr || capacity == 0) {
    return executor_count_;
  }
  const size_t count = capacity < executor_count_ ? capacity : executor_count_;
  for (size_t index = 0; index < count; ++index) {
    const ServiceExecutorState &executor = executors_[index];
    output[index] = {
        .id = executor.id,
        .owner_service_id = executor.owner_service_id,
        .queue_depth = executor.queue_depth,
        .bound = executor.task_handle.load(std::memory_order_acquire) != 0,
        .pending_calls = executor.pending_calls.load(std::memory_order_acquire),
    };
  }
  return executor_count_;
}

uint32_t ServiceExecutorRegistry::pending_calls(const char *id) const {
  const auto *executor = find(id);
  return executor ? executor->pending_calls.load(std::memory_order_acquire) : 0;
}

#ifdef ESP_PLATFORM
bool ServiceExecutorRegistry::bind(const char *executor_id, std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  ServiceExecutorState *executor = find(executor_id);
  if (executor == nullptr) {
    set_error(error, "service_executor_not_found");
    return false;
  }

  const uintptr_t current =
      reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  uintptr_t existing = 0;
  if (!executor->task_handle.compare_exchange_strong(
          existing, current, std::memory_order_acq_rel) &&
      existing != current) {
    set_error(error, "service_executor_already_bound");
    return false;
  }
  // An idempotent bind cannot reopen an owner already draining. Only a new
  // binding after acknowledged withdrawal starts another admission lifetime.
  if (existing == 0) executor->retiring.store(false, std::memory_order_release);
  return true;
}

namespace {
class ExecutorAdmissionGuard {
public:
  explicit ExecutorAdmissionGuard(std::atomic_flag &lock) : lock_(lock) {
    while (lock_.test_and_set(std::memory_order_acquire)) {
      // Let a lower-priority owner/producer finish admission on the same core.
      vTaskDelay(1);
    }
    acquired_ = true;
  }
  ExecutorAdmissionGuard(std::atomic_flag &lock, TickType_t started,
                         TickType_t budget) : lock_(lock) {
    while (lock_.test_and_set(std::memory_order_acquire)) {
      if (budget != portMAX_DELAY && xTaskGetTickCount() - started >= budget) return;
      vTaskDelay(1);
    }
    acquired_ = true;
  }
  bool acquired() const { return acquired_; }
  ~ExecutorAdmissionGuard() { if (acquired_) lock_.clear(std::memory_order_release); }
private:
  std::atomic_flag &lock_;
  bool acquired_ = false;
};

class InvocationDemand {
public:
  explicit InvocationDemand(ServiceExecutorState &executor) : executor_(executor) {}
  bool acquire(uint32_t limit = UINT32_MAX) {
    auto count = executor_.pending_calls.load(std::memory_order_relaxed);
    do {
      if (count >= limit) return false;
    } while (!executor_.pending_calls.compare_exchange_weak(count, count + 1,
        std::memory_order_acq_rel, std::memory_order_relaxed));
    acquired_ = true;
    return true;
  }
  ~InvocationDemand() {
    if (acquired_) executor_.pending_calls.fetch_sub(1, std::memory_order_release);
  }
  void transfer() { acquired_ = false; }
private:
  ServiceExecutorState &executor_;
  bool acquired_ = false;
};
}

bool ServiceExecutorRegistry::prepare_retirement(const char *executor_id,
                                                 std::string *error) {
  auto *executor = find(executor_id);
  if (!executor) { set_error(error, "service_executor_not_found"); return false; }
  // A producer may hold this gate while waiting for queue space. Do not
  // wait behind it: the owner must pump accepted work to release that space.
  ExecutorAdmissionGuard admission(executor->admission_lock, xTaskGetTickCount(), 0);
  if (!admission.acquired()) {
    set_error(error, "service_executor_admission_busy"); return false;
  }
  const auto current = reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  if (executor->task_handle.load(std::memory_order_acquire) != current) {
    set_error(error, "service_executor_wrong_owner_task"); return false;
  }
  executor->retiring.store(true, std::memory_order_release);
  // Count completion waiters as well as queued work. A released queue alone
  // must not allow restart to reuse a task handle still borrowed by a caller.
  if (uxQueueMessagesWaiting(executor->queue) != 0 ||
      executor->pending_calls.load(std::memory_order_acquire) != 0) {
    set_error(error, "service_executor_not_quiescent"); return false;
  }
  if (error) error->clear();
  return true;
}

bool ServiceExecutorRegistry::unbind(const char *executor_id,
                                     std::string *error) {
  auto *executor = find(executor_id);
  if (!executor) {
    set_error(error, "service_executor_not_found");
    return false;
  }
  ExecutorAdmissionGuard admission(executor->admission_lock);
  if (uxQueueMessagesWaiting(executor->queue) != 0 ||
      executor->pending_calls.load(std::memory_order_acquire) != 0) {
    set_error(error, "service_executor_not_quiescent");
    return false;
  }
  uintptr_t owner = reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  if (!executor->task_handle.compare_exchange_strong(owner, 0)) {
    set_error(error, "service_executor_wrong_owner_task");
    return false;
  }
  if (error)
    error->clear();
  return true;
}

bool ServiceExecutorRegistry::pump(const char *executor_id,
                                   uint32_t wait_timeout_ms,
                                   std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  ServiceExecutorState *executor = find(executor_id);
  if (executor == nullptr || executor->queue == nullptr) {
    set_error(error, "service_executor_unavailable");
    return false;
  }
  const uintptr_t current =
      reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  const uintptr_t owner =
      executor->task_handle.load(std::memory_order_acquire);
  if (owner == 0) {
    if (!bind(executor_id, error)) {
      return false;
    }
  } else if (owner != current) {
    set_error(error, "service_executor_wrong_owner_task");
    return false;
  }

  if (uxQueueMessagesWaiting(executor->queue) == 0) {
    lunaredu::lunaros::task_signal::wait_ms(wait_timeout_ms);
  }

  ServiceInvocation invocation;
  // Arrivals during callbacks belong to a later round. A bounded queue alone
  // does not bound an unbounded drain loop under continuous producers.
  const UBaseType_t pending = uxQueueMessagesWaiting(executor->queue);
  for (UBaseType_t index = 0; index < pending &&
       xQueueReceive(executor->queue, &invocation, 0) == pdPASS; ++index) {
    execute(invocation);
    if (invocation.completion != nullptr) {
      (void)xSemaphoreGive(invocation.completion);
    }
  }
  return true;
}

bool ServiceExecutorRegistry::current_task_owns() const {
  const uintptr_t current =
      reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  for (size_t index = 0; index < executor_count_; ++index) {
    if (current != 0 && executors_[index].task_handle.load(
                            std::memory_order_acquire) == current) return true;
  }
  return false;
}

bool ServiceExecutorRegistry::current_task_owns(const char *id) const {
  const auto *executor = find(id);
  const uintptr_t current = reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  return executor && current && executor->task_handle.load(std::memory_order_acquire) == current;
}

bool ServiceExecutorRegistry::submit_retained(const char *id, std::unique_ptr<ServiceRequest> &request,
    ServiceCall &call, std::string &error, uint32_t timeout_ms,bool queue_only) {
  error.clear();
  auto *executor = find(id);
  if (!executor || !executor->queue) { error = "service_executor_unavailable"; return false; }
  if (!request || call.valid() || timeout_ms == UINT32_MAX) {
    error = "invalid_owned_service_operation"; return false;
  }
  const auto current = reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  if (executor->retiring.load(std::memory_order_acquire) &&
      executor->task_handle.load(std::memory_order_acquire) != current) {
    error = "service_executor_retiring"; return false;
  }
  InvocationDemand demand(*executor);
  if (!demand.acquire(executor->queue_depth)) { error = "service_executor_call_capacity"; return false; }
  ServiceCall candidate;
  candidate.state_ = new (std::nothrow) ServiceCall::State;
  if (!candidate.state_) { error = "service_executor_request_allocation_failed"; return false; }
  candidate.state_->request = std::move(request);
  bool transferred=false;
  struct RestoreRequest {
    ServiceCall::State *state;
    std::unique_ptr<ServiceRequest> &request;
    bool &transferred;
    ~RestoreRequest(){if(!transferred)request=std::move(state->request);}
  } restore{candidate.state_,request,transferred};
  candidate.state_->registry = this;
  candidate.state_->executor = executor;
  const TickType_t started = xTaskGetTickCount();
  const TickType_t budget = timeout_ms ? std::max<TickType_t>(1, pdMS_TO_TICKS(timeout_ms)) : 0;
  uintptr_t owner = executor->task_handle.load(std::memory_order_acquire);
  if (!owner && executor->activation.request) {
    executor->activation.request(executor->activation.context);
    while (!(owner = executor->task_handle.load(std::memory_order_acquire))) {
      if (xTaskGetTickCount() - started >= budget) { error = "service_executor_activation_timeout"; return false; }
      vTaskDelay(1);
    }
  }
  if (!owner) { error = "service_executor_unbound"; return false; }
  candidate.state_->owner = owner;
  if (owner == current && !queue_only) candidate.state_->execute();
  else {
    ExecutorAdmissionGuard admission(executor->admission_lock, started, budget);
    if (!admission.acquired()) { error = "service_executor_admission_timeout"; return false; }
    if (executor->retiring.load(std::memory_order_acquire)) { error = "service_executor_retiring"; return false; }
    if (executor->task_handle.load(std::memory_order_acquire) != owner) { error = "service_executor_unbound"; return false; }
    const auto elapsed = xTaskGetTickCount() - started;
    if (elapsed > budget) { error = "service_executor_admission_timeout"; return false; }
    const ServiceInvocation invocation{nullptr, nullptr, nullptr, nullptr, candidate.state_};
    candidate.state_->retain(); // Queue, independent of the caller ticket.
    if (xQueueSend(executor->queue, &invocation, elapsed >= budget ? 0 : budget - elapsed) != pdPASS) {
      candidate.state_->release(); error = "service_executor_queue_full"; return false;
    }
    demand.transfer(); // Pump now owns this in-flight demand until execution ends.
    task_signal::notify(reinterpret_cast<TaskHandle_t>(owner));
  }
  transferred=true;
  call = std::move(candidate);
  return true;
}

bool ServiceExecutorRegistry::wait(ServiceCall &call, uint32_t timeout_ms, std::string &error) {
  error.clear();
  if (!call.valid()) { error = "invalid_service_call"; return false; }
  if (call.state_->registry != this) { error = "service_call_runtime_mismatch"; return false; }
  if (call.ready()) return true;
  if (timeout_ms == UINT32_MAX) { error = "service_executor_completion_deadline_required"; return false; }
  const auto current = reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  OwnerWait waiting(executors_, executor_count_, current, call.state_->owner);
  if (waiting.cyclic(call.state_->owner)) { error = "service_executor_dependency_cycle"; return false; }
  const TickType_t started = xTaskGetTickCount();
  const TickType_t budget = timeout_ms ? std::max<TickType_t>(1, pdMS_TO_TICKS(timeout_ms)) : 0;
  while (!call.ready()) {
    if (xTaskGetTickCount() - started >= budget) { error = "service_executor_completion_timeout"; return false; }
    vTaskDelay(1); // Do not consume SDK or executor task notifications.
  }
  return true;
}

bool ServiceExecutorRegistry::invoke(const char *executor_id,
                                     ServiceOperation operation, void *context,
                                     std::string &error,
                                     uint32_t admission_timeout_ms) {
  error.clear();
  ServiceExecutorState *executor = find(executor_id);
  if (executor == nullptr || executor->queue == nullptr) {
    error = "service_executor_unavailable";
    return false;
  }
  if (operation == nullptr) {
    error = "invalid_service_operation";
    return false;
  }
  const auto current = reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
  if (executor->retiring.load(std::memory_order_acquire) &&
      executor->task_handle.load(std::memory_order_acquire) != current) {
    error = "service_executor_retiring"; return false;
  }
  InvocationDemand demand(*executor);
  if (!demand.acquire()) {
    error = "service_executor_call_capacity";
    return false;
  }
  const TickType_t admission_ticks = admission_timeout_ms == UINT32_MAX
      ? portMAX_DELAY : pdMS_TO_TICKS(admission_timeout_ms);
  const TickType_t admission_started = xTaskGetTickCount();
  ServiceOperationResult result;
  const auto execution=capture_operation_context();
  ServiceInvocation invocation{operation, context, &result, nullptr,nullptr,&execution};
  uintptr_t owner =
      executor->task_handle.load(std::memory_order_acquire);
  if (!owner && executor->activation.request) {
    executor->activation.request(executor->activation.context);
    // There is no accepted operation yet. An activation timeout releases only
    // this caller's demand; a later owner must not execute its stack context.
    while (!(owner = executor->task_handle.load(std::memory_order_acquire))) {
      if (admission_ticks != portMAX_DELAY &&
          xTaskGetTickCount() - admission_started >= admission_ticks) {
        error = "service_executor_activation_timeout";
        return false;
      }
      vTaskDelay(1);
    }
    if (admission_ticks != portMAX_DELAY &&
        xTaskGetTickCount() - admission_started > admission_ticks) {
      error = "service_executor_activation_timeout";
      return false;
    }
  }
  if (owner != 0 && current == owner) {
    execute(invocation);
  } else {
    if (owner == 0) {
      error = "service_executor_unbound";
      return false;
    }
    OwnerWait waiting(executors_, executor_count_, current, owner);
    if (waiting.cyclic(owner)) {
      error = "service_executor_dependency_cycle";
      return false;
    }
    StaticSemaphore_t completion_storage{};
    SemaphoreHandle_t completion =
        xSemaphoreCreateBinaryStatic(&completion_storage);
    if (completion == nullptr) {
      error = "service_executor_completion_allocation_failed";
      return false;
    }
    invocation.completion = completion;
    {
      // Unbinding and admission share a gate: a retiring owner must never
      // leave an accepted invocation waiting on a deleted task handle.
      ExecutorAdmissionGuard admission(executor->admission_lock, admission_started, admission_ticks);
      if (!admission.acquired()) {
        error = "service_executor_admission_timeout";
        return false;
      }
      if (executor->retiring.load(std::memory_order_acquire)) {
        error = "service_executor_retiring"; return false;
      }
      if (executor->task_handle.load(std::memory_order_acquire) != owner) {
        error = "service_executor_unbound";
        return false;
      }
      const TickType_t elapsed = xTaskGetTickCount() - admission_started;
      if (admission_ticks != portMAX_DELAY && elapsed > admission_ticks) {
        error = "service_executor_admission_timeout";
        return false;
      }
      const TickType_t queue_ticks = admission_ticks == portMAX_DELAY ? portMAX_DELAY :
          elapsed >= admission_ticks ? 0 : admission_ticks - elapsed;
      if (xQueueSend(executor->queue, &invocation, queue_ticks) != pdPASS) {
        error = "service_executor_queue_full";
        return false;
      }
      lunaredu::lunaros::task_signal::notify(reinterpret_cast<TaskHandle_t>(owner));
    }
    if (xSemaphoreTake(completion, portMAX_DELAY) != pdTRUE) {
      error = "service_executor_completion_wait_failed";
      return false;
    }
  }

  if (!result.ok) {
    error = result.error.empty() ? "service_operation_failed" : result.error;
  }
  return result.ok;
}

#else
// Explicit single-owner host ports supply invoke. This seam performs the same
// ownership/result publication but does not model foreign RTOS queueing.
bool ServiceExecutorRegistry::submit_retained(const char *id, std::unique_ptr<ServiceRequest> &request,
    ServiceCall &call, std::string &error, uint32_t timeout,bool queue_only) {
  if(queue_only){error="host_deferred_service_owner_required";return false;}
  if (!request || call.valid() || timeout == UINT32_MAX) { error = "invalid_owned_service_operation"; return false; }
  ServiceCall candidate;
  candidate.state_ = new (std::nothrow) ServiceCall::State;
  if (!candidate.state_) { error = "service_executor_request_allocation_failed"; return false; }
  candidate.state_->request = std::move(request);
  bool transferred=false;
  struct RestoreRequest {
    ServiceCall::State *state;
    std::unique_ptr<ServiceRequest> &request;
    bool &transferred;
    ~RestoreRequest(){if(!transferred)request=std::move(state->request);}
  } restore{candidate.state_,request,transferred};
  candidate.state_->registry = this;
  bool ran = false;
  struct Context { ServiceCall::State *state; bool &ran; } context{candidate.state_, ran};
  const bool admitted = invoke(id, [](void *opaque, ServiceOperationResult &) {
    auto &context = *static_cast<Context *>(opaque); context.ran = true; context.state->execute();
  }, &context, error, timeout);
  if (!ran) return false;
  (void)admitted; // A returned callback owns its result even if work reported failure.
  transferred=true;
  call = std::move(candidate); error.clear(); return true;
}
bool ServiceExecutorRegistry::wait(ServiceCall &call, uint32_t, std::string &error) {
  error.clear();
  if(call.valid() && call.state_->registry!=this){error="service_call_runtime_mismatch";return false;}
  if (call.ready()) return true;
  error = call.valid() ? "host_service_call_not_completed" : "invalid_service_call";
  return false;
}
#endif

bool ServiceExecutorRegistry::submit(const char *id,std::unique_ptr<ServiceRequest> request,
    ServiceCall &call,std::string &error,uint32_t timeout,bool queue_only) {
  return submit_retained(id,request,call,error,timeout,queue_only);
}
ExecutorReadiness ServiceExecutorRegistry::request_activation(const char *id) {
  auto *executor=find(id);
  if(!executor)return ExecutorReadiness::Unavailable;
  if(executor->task_handle.load(std::memory_order_acquire)&&
     !executor->retiring.load(std::memory_order_acquire))return ExecutorReadiness::Ready;
  if(!executor->activation.request)return ExecutorReadiness::Unavailable;
  executor->activation.request(executor->activation.context);
  return ExecutorReadiness::Pending;
}

} // namespace lunaredu::lunaros
