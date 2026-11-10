#include "model/device_activity.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <memory>
#include <cstring>

#include "kernel/static_mutex.h"

namespace lunaredu::lunaros {
namespace {

std::string utc_now() {
  const std::time_t now = std::time(nullptr);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &now);
#else
  gmtime_r(&now, &utc);
#endif
  char value[32]{};
  if (std::strftime(value, sizeof(value), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0)
    return "1970-01-01T00:00:00Z";
  return value;
}

bool valid_text(const char *value) { return value != nullptr && value[0] != '\0'; }
bool bounded_text(const char *value, size_t limit) {
  if (!valid_text(value)) return false;
  size_t n=0; while(n<=limit && value[n]) ++n; return n<=limit;
}
void copy_message(std::string &out, const char *value, bool &truncated) {
  if (!value) return;
  size_t n=0; while(n<=kDeviceActivityMessageBytes && value[n]) ++n;
  if(n>kDeviceActivityMessageBytes) {
    n=kDeviceActivityMessageBytes;
    // Do not split a valid UTF-8 character at the byte budget.
    while(n && (static_cast<unsigned char>(value[n])&0xc0)==0x80) --n;
    truncated=true;
  }
  out.assign(value,n);
}

} // namespace

void format_device_request_id(char *out, size_t capacity, uint32_t sequence) {
  std::snprintf(out, capacity, "req_device-%08lx", static_cast<unsigned long>(sequence));
}

const char *device_activity_state_name(DeviceActivityState state) {
  switch (state) {
  case DeviceActivityState::Accepted: return "accepted";
  case DeviceActivityState::Running: return "running";
  case DeviceActivityState::Succeeded: return "succeeded";
  case DeviceActivityState::Failed: return "failed";
  case DeviceActivityState::Canceled: return "canceled";
  case DeviceActivityState::TimedOut: return "timed_out";
  }
  return "failed";
}

bool device_activity_terminal(DeviceActivityState state) {
  return state == DeviceActivityState::Succeeded ||
         state == DeviceActivityState::Failed ||
         state == DeviceActivityState::Canceled ||
         state == DeviceActivityState::TimedOut;
}

struct DeviceActivityRegistry::Impl {
  std::array<DeviceActivitySnapshot, kDeviceActivityCapacity> items{};
  size_t count = 0;
  std::array<uint8_t,kDeviceActivityCapacity> order{}; // Oldest to newest.
  uint32_t next_id = 1;
  uint32_t revision = 0;
  mutable StaticMutex mutex;
};

DeviceActivityRegistry::DeviceActivityRegistry() : impl_(new Impl) {}
DeviceActivityRegistry::~DeviceActivityRegistry() { delete impl_; }

void DeviceActivityRegistry::reset() {
  StaticLockGuard lock(impl_->mutex);
  // Reset one slot at a time. Assigning {} to the whole array materializes
  // a 32-record temporary on Xtensa (9,776-byte frame), exceeding the 8 KiB
  // boot task before networking can expose diagnostics.
  for (auto &item : impl_->items) item = {};
  impl_->count = 0; impl_->order.fill(0);
  impl_->next_id = 1;
  ++impl_->revision;
}

std::string DeviceActivityRegistry::accept(const char *capability_id,
                                           const char *operation_id,
                                           const char *message,
                                           bool cancelable) {
  if (!bounded_text(capability_id,kDeviceActivityIdentifierBytes) ||
      !bounded_text(operation_id,kDeviceActivityIdentifierBytes) ||
      (valid_text(message) && !bounded_text(message,kDeviceActivityMessageBytes))) return {};
  StaticLockGuard lock(impl_->mutex);
  if (!impl_->next_id) return {}; // Never recycle an identity in this lifecycle.
  size_t slot=impl_->count;
  if(impl_->count==kDeviceActivityCapacity) {
    size_t oldest=0;
    while(oldest<impl_->count && !device_activity_terminal(impl_->items[impl_->order[oldest]].state)) ++oldest;
    if(oldest==impl_->count) return {};
    slot=impl_->order[oldest];
    for(size_t i=oldest+1;i<impl_->count;++i) impl_->order[i-1]=impl_->order[i];
    --impl_->count;
  }
  const uint32_t sequence = impl_->next_id++;
  char activity_id[32]{};
  char request_id[32]{};
  std::snprintf(activity_id, sizeof(activity_id), "act_local-%08lx",
                static_cast<unsigned long>(sequence));
  std::snprintf(request_id, sizeof(request_id), "req_local-%08lx",
                static_cast<unsigned long>(sequence));
  DeviceActivitySnapshot snapshot;
  snapshot.activity_id = activity_id;
  snapshot.request_id = request_id;
  snapshot.capability_id = capability_id;
  snapshot.operation_id = operation_id;
  snapshot.state = DeviceActivityState::Accepted;
  snapshot.accepted_at = snapshot.updated_at = utc_now();
  snapshot.progress_message = valid_text(message) ? message : "accepted";
  snapshot.cancelable = cancelable;
  snapshot.revision = ++impl_->revision;
  impl_->items[slot] = std::move(snapshot);
  impl_->order[impl_->count++]=static_cast<uint8_t>(slot);
  return activity_id;
}

namespace {
template <typename State>
DeviceActivitySnapshot *find_activity(State &state, const char *activity_id) {
  if (!bounded_text(activity_id,kDeviceActivityIdentifierBytes)) return nullptr;
  for (auto &item : state.items) {
    if (item.activity_id == activity_id) return &item;
  }
  return nullptr;
}

template <typename State>
bool transition(State &state, const char *activity_id, DeviceActivityState next,
                int progress_percent, const char *message) {
  auto *item = find_activity(state, activity_id);
  if (item == nullptr || device_activity_terminal(item->state)) return false;
  const std::string now = utc_now();
  if (next == DeviceActivityState::Running && item->started_at.empty())
    item->started_at = now;
  item->state = next;
  item->updated_at = now;
  if (device_activity_terminal(next)) item->finished_at = now;
  item->progress_percent = std::clamp(progress_percent, 0, 100);
  if (valid_text(message)) copy_message(item->progress_message,message,item->details_truncated);
  item->revision = ++state.revision;
  return true;
}
} // namespace

bool DeviceActivityRegistry::start(const char *activity_id,
                                   const char *message) {
  StaticLockGuard lock(impl_->mutex);
  return transition(*impl_, activity_id, DeviceActivityState::Running, 0,
                    message ? message : "running");
}

bool DeviceActivityRegistry::progress(const char *activity_id, int percent,
                                      const char *message) {
  StaticLockGuard lock(impl_->mutex);
  return transition(*impl_, activity_id, DeviceActivityState::Running, percent,
                    message);
}

bool DeviceActivityRegistry::succeed(const char *activity_id,
                                     const char *result_json,
                                     const char *message) {
  StaticLockGuard lock(impl_->mutex);
  if (!transition(*impl_, activity_id, DeviceActivityState::Succeeded, 100,
                  message ? message : "succeeded"))
    return false;
  auto *item = find_activity(*impl_, activity_id);
  if (item != nullptr && valid_text(result_json)) {
    if (bounded_text(result_json,kDeviceActivityResultBytes)) item->result_json = result_json;
    else { item->result_json.clear(); item->result_omitted=true; }
  }
  return true;
}

bool DeviceActivityRegistry::fail(const char *activity_id,
                                  const char *error_code,
                                  const char *error_message) {
  StaticLockGuard lock(impl_->mutex);
  if (!transition(*impl_, activity_id, DeviceActivityState::Failed, 100,
                  error_message ? error_message : "failed"))
    return false;
  auto *item = find_activity(*impl_, activity_id);
  if (item != nullptr) {
    const bool valid_code=bounded_text(error_code,kDeviceActivityIdentifierBytes);
    item->error_code = valid_code ? error_code : "activity_failed";
    if(valid_text(error_code) && !valid_code) item->details_truncated=true;
    copy_message(item->error_message,valid_text(error_message)?error_message:item->error_code.c_str(),item->details_truncated);
  }
  return true;
}

bool DeviceActivityRegistry::cancel(const char *activity_id,
                                    const char *message) {
  StaticLockGuard lock(impl_->mutex);
  auto *item = find_activity(*impl_, activity_id);
  if (item == nullptr || !item->cancelable || device_activity_terminal(item->state))
    return false;
  return transition(*impl_, activity_id, DeviceActivityState::Canceled,
                    item->progress_percent,
                    message ? message : "canceled");
}

bool DeviceActivityRegistry::get(const char *activity_id,
                                 DeviceActivitySnapshot &snapshot) const {
  StaticLockGuard lock(impl_->mutex);
  auto *item = find_activity(*impl_, activity_id);
  if (item == nullptr) return false;
  snapshot = *item;
  return true;
}

bool DeviceActivityRegistry::correlate(const char *activity_id, const char *capability_id,
    const char *operation_id, uint32_t request_sequence, uint64_t trace_id) {
  if (!request_sequence || !bounded_text(capability_id,kDeviceActivityIdentifierBytes) || !bounded_text(operation_id,kDeviceActivityIdentifierBytes)) return false;
  StaticLockGuard lock(impl_->mutex);
  auto *item = find_activity(*impl_, activity_id);
  if (!item || item->capability_id != capability_id || item->operation_id != operation_id) return false;
  if (item->ingress_request_sequence != 0)
    return item->ingress_request_sequence == request_sequence && item->trace_id == trace_id;
  char request[32];
  format_device_request_id(request, sizeof(request), request_sequence);
  item->request_id = request;
  item->ingress_request_sequence = request_sequence;
  item->trace_id = trace_id;
  item->revision = ++impl_->revision;
  return true;
}

DeviceActivityObservationSummary DeviceActivityRegistry::observe(size_t offset, size_t limit,
    DeviceActivityObservationVisitor visitor, void *context) const {
  DeviceActivityObservationSummary summary;
  std::array<uint8_t,kDeviceActivityCapacity> order{};
  {
    StaticLockGuard lock(impl_->mutex);
    summary.total = impl_->count;
    summary.revision_before = impl_->revision;
    order = impl_->order;
  }
  if (visitor) for (size_t i = offset; i < summary.total && summary.returned < limit; ++i) {
    DeviceActivityObservation observation;
    {
      StaticLockGuard lock(impl_->mutex);
      if (impl_->revision != summary.revision_before) break;
      const auto &source = impl_->items[order[summary.total-1-i]];
      const auto copy = [&](auto &target, const std::string &text) {
        const size_t count = std::min(target.size() - 1, text.size());
        std::memcpy(target.data(), text.data(), count);
        observation.truncated |= text.size() >= target.size();
      };
      copy(observation.activity_id, source.activity_id);
      copy(observation.request_id, source.request_id);
      copy(observation.capability_id, source.capability_id);
      copy(observation.operation_id, source.operation_id);
      copy(observation.error_code, source.error_code);
      observation.state = source.state;
      observation.trace_id = source.trace_id;
      observation.revision = source.revision;
    }
    visitor(observation, context);
    ++summary.returned;
  }
  summary.revision_after = revision();
  summary.changed = summary.revision_before != summary.revision_after;
  return summary;
}

void DeviceActivityRegistry::visit(DeviceActivityVisitor visitor,
                                   void *context) const {
  if (visitor == nullptr) return;
  // A snapshot owns several std::string fields. Keeping the complete bounded
  // history on the caller's stack overflows the S31 main task while an HTTP
  // query is being rendered. Allocate the temporary copy from the heap, but
  // preserve the important lock boundary: copy while locked, invoke visitors
  // only after releasing the registry mutex.
  auto copy = std::make_unique<
      std::array<DeviceActivitySnapshot, kDeviceActivityCapacity>>();
  size_t count = 0;
  {
    StaticLockGuard lock(impl_->mutex);
    count = impl_->count;
    for (size_t offset = 0; offset < count; ++offset) {
      const size_t index = impl_->order[count-1-offset];
      (*copy)[offset] = impl_->items[index];
    }
  }
  for (size_t index = 0; index < count; ++index)
    visitor((*copy)[index], context);
}

uint32_t DeviceActivityRegistry::revision() const {
  StaticLockGuard lock(impl_->mutex);
  return impl_->revision;
}

} // namespace lunaredu::lunaros
