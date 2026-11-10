#include "control/output_control.h"

#include <cstring>
#include <limits>

namespace lunaredu::lunaros {
namespace {
bool valid(const char *value) { return value && value[0]; }
bool same(const char *a, const char *b) {
  return valid(a) && valid(b) && std::strcmp(a, b) == 0;
}
bool fail(std::string &error, const char *reason) {
  error = reason;
  return false;
}
} // namespace

OutputControl::Slot *OutputControl::find_output_locked(const char *output) {
  for (size_t i = 0; i < output_count_; ++i)
    if (same(outputs_[i].provider.output, output)) return &outputs_[i];
  return nullptr;
}
const OutputControl::Consumer *OutputControl::find_consumer_locked(
    const char *output, const char *caller) const {
  for (size_t i = 0; i < consumer_count_; ++i)
    if (same(consumers_[i].output, output) &&
        same(consumers_[i].caller, caller)) return &consumers_[i];
  return nullptr;
}
bool OutputControl::expired(const Slot &slot, const Lease &lease, uint32_t now_ms) {
  return lease.id && slot.provider.maximum_lease_ms &&
      static_cast<int32_t>(now_ms - lease.deadline_ms) >= 0;
}
bool OutputControl::can_reset_locked(std::string &error) const {
  for (size_t i = 0; i < output_count_; ++i)
    if (outputs_[i].lease.id || outputs_[i].pending.id || outputs_[i].owner)
      return fail(error, "output_control_retirement_required");
  error.clear(); return true;
}
bool OutputControl::can_reset(std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  return can_reset_locked(error);
}
bool OutputControl::reset(std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!can_reset_locked(error)) return false;
  outputs_.fill({}); consumers_.fill({});
  output_count_ = consumer_count_ = 0; sealed_ = false;
  error.clear(); return true; // generation survives composition reset
}
bool OutputControl::ready(const char *output, const char *provider) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto *slot = find_output_locked(output);
  return sealed_ && slot && same(slot->provider.id, provider);
}
bool OutputControl::attach_provider(const char *output, const char *provider,
                                    const void *owner, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto *slot = find_output_locked(output);
  if (!sealed_ || !slot || !same(slot->provider.id, provider) || !owner)
    return fail(error, "output_provider_denied");
  if (slot->owner && slot->owner != owner) return fail(error, "output_provider_owned");
  if (!slot->owner && (slot->lease.id || slot->pending.id))
    return fail(error, "output_control_retirement_required");
  slot->owner = owner; error.clear(); return true;
}
bool OutputControl::detach_provider(const char *output, const char *provider,
                                    const void *owner, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto *slot = find_output_locked(output);
  if (!slot || !owner || slot->owner != owner || !same(slot->provider.id, provider))
    return fail(error, "output_provider_denied");
  if (slot->lease.id || slot->pending.id) return fail(error, "output_control_retirement_required");
  slot->owner = nullptr; error.clear(); return true;
}
bool OutputControl::register_provider(Provider provider, std::string &error) {
  if (!valid(provider.output) || !valid(provider.id) ||
      provider.maximum_lease_ms > INT32_MAX)
    return fail(error, "invalid_output_provider");
  std::lock_guard<std::mutex> lock(mutex_);
  if (sealed_) return fail(error, "output_control_sealed");
  if (find_output_locked(provider.output))
    return fail(error, "duplicate_output_provider");
  if (output_count_ == outputs_.size())
    return fail(error, "output_provider_capacity");
  outputs_[output_count_++].provider = provider;
  error.clear();
  return true;
}
bool OutputControl::register_consumer(Consumer consumer, std::string &error) {
  if (!valid(consumer.caller) || !valid(consumer.output))
    return fail(error, "invalid_output_consumer");
  std::lock_guard<std::mutex> lock(mutex_);
  if (sealed_) return fail(error, "output_control_sealed");
  if (find_consumer_locked(consumer.output, consumer.caller))
    return fail(error, "duplicate_output_consumer");
  if (consumer_count_ == consumers_.size())
    return fail(error, "output_consumer_capacity");
  consumers_[consumer_count_++] = consumer;
  error.clear();
  return true;
}
bool OutputControl::seal(std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (size_t i = 0; i < consumer_count_; ++i) {
    bool found = false;
    for (size_t j = 0; j < output_count_; ++j)
      found |= same(consumers_[i].output, outputs_[j].provider.output);
    if (!found) return fail(error, "output_consumer_provider_missing");
  }
  sealed_ = true;
  error.clear();
  return true;
}
bool OutputControl::admit(const char *output, const char *caller,
                            uint32_t lease_ms, uint32_t now_ms,
                            uint64_t replace_id, Admission &admission,
                            std::string &error) {
  admission = {};
  if (!valid(output) || !valid(caller))
    return fail(error, "invalid_output_request");
  std::lock_guard<std::mutex> lock(mutex_);
  if (!sealed_) return fail(error, "output_control_unsealed");
  Slot *slot = find_output_locked(output);
  const Consumer *consumer = find_consumer_locked(output, caller);
  if (!slot || !consumer) return fail(error, "output_control_denied");
  if ((slot->provider.maximum_lease_ms == 0 && lease_ms != 0) ||
      (slot->provider.maximum_lease_ms != 0 &&
       (lease_ms == 0 || lease_ms > slot->provider.maximum_lease_ms)))
    return fail(error, "invalid_output_lease");
  if (slot->pending.id || slot->retiring) return fail(error, "output_handoff_pending");
  if (replace_id && (!slot->lease.id || slot->lease.id != replace_id ||
                     !same(slot->lease.caller, caller)))
    return fail(error, "stale_output_lease");
  if (slot->lease.id && !expired(*slot, slot->lease, now_ms) && slot->lease.id != replace_id &&
      (!slot->provider.preemptible ||
       consumer->priority <= slot->lease.priority))
    return fail(error, "output_busy");
  if (generation_ == std::numeric_limits<uint64_t>::max())
    return fail(error, "output_generation_exhausted");
  slot->pending = {++generation_, slot->provider.output, consumer->caller,
                 lease_ms ? now_ms + lease_ms : 0, consumer->priority};
  admission = {slot->pending, slot->lease};
  error.clear();
  return true;
}
bool OutputControl::renew(uint64_t id, const char *caller,
                          uint32_t lease_ms, uint32_t now_ms,
                          std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (size_t i = 0; i < output_count_; ++i) {
    auto &slot = outputs_[i];
    if (slot.pending.id || slot.retiring || expired(slot, slot.lease, now_ms)) continue;
    if (!id || slot.lease.id != id || !same(slot.lease.caller, caller)) continue;
    if (!slot.provider.maximum_lease_ms || !lease_ms ||
        lease_ms > slot.provider.maximum_lease_ms)
      return fail(error, "invalid_output_lease");
    slot.lease.deadline_ms = now_ms + lease_ms;
    error.clear();
    return true;
  }
  return fail(error, "stale_output_lease");
}
bool OutputControl::confirm(uint64_t id, const char *provider, uint32_t now_ms,
                            Lease &granted, std::string &error) {
  granted = {};
  std::lock_guard<std::mutex> lock(mutex_);
  for (size_t i = 0; i < output_count_; ++i) {
    auto &slot = outputs_[i];
    if (!id || slot.pending.id != id) continue;
    if (!same(slot.provider.id, provider)) return fail(error, "output_provider_denied");
    if (expired(slot, slot.pending, now_ms)) return fail(error, "output_admission_expired");
    slot.lease = slot.pending; slot.pending = {}; granted = slot.lease;
    error.clear(); return true;
  }
  return fail(error, "stale_output_admission");
}
bool OutputControl::cancel(uint64_t id, const char *provider, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (size_t i = 0; i < output_count_; ++i) {
    auto &slot = outputs_[i];
    if (!id || slot.pending.id != id) continue;
    if (!same(slot.provider.id, provider)) return fail(error, "output_provider_denied");
    slot.pending = {}; slot.lease = {}; slot.retiring = false; error.clear(); return true;
  }
  return fail(error, "stale_output_admission");
}
bool OutputControl::begin_retirement(uint64_t id, const char *provider, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (size_t i = 0; i < output_count_; ++i) {
    auto &slot = outputs_[i];
    if (!id || slot.lease.id != id) continue;
    if (!same(slot.provider.id, provider)) return fail(error, "output_provider_denied");
    if (slot.pending.id) return fail(error, "output_handoff_pending");
    slot.retiring = true; error.clear(); return true;
  }
  return fail(error, "stale_output_lease");
}
bool OutputControl::retire(uint64_t id, const char *provider, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (size_t i = 0; i < output_count_; ++i) {
    auto &slot = outputs_[i];
    if (!id || slot.lease.id != id) continue;
    if (!same(slot.provider.id, provider)) return fail(error, "output_provider_denied");
    if (slot.pending.id) return fail(error, "output_handoff_pending");
    slot.lease = {}; slot.retiring = false; error.clear(); return true;
  }
  return fail(error, "stale_output_lease");
}
bool OutputControl::holds(uint64_t id, const char *caller, uint32_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (size_t i = 0; i < output_count_; ++i) {
    if (outputs_[i].pending.id || outputs_[i].retiring || expired(outputs_[i], outputs_[i].lease, now_ms)) continue;
    if (id && outputs_[i].lease.id == id &&
        same(outputs_[i].lease.caller, caller)) return true;
  }
  return false;
}
OutputControl::Lease OutputControl::snapshot(const char *output,
                                             uint32_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  Slot *slot = find_output_locked(output);
  if (!slot || slot->pending.id || slot->retiring || expired(*slot, slot->lease, now_ms)) return {};
  return slot->lease;
}
size_t OutputControl::observe(Observation *output, size_t capacity, uint32_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (size_t i = 0; output && i < output_count_ && i < capacity; ++i) {
    const auto &slot = outputs_[i];
    output[i] = {slot.provider, slot.lease,
        expired(slot, slot.lease, now_ms), slot.pending, slot.retiring, slot.owner != nullptr};
  }
  return output_count_;
}
OutputControl::Observation OutputControl::observe(const char *output, uint32_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto *slot = find_output_locked(output);
  if (!slot) return {};
  return {slot->provider, slot->lease,
      expired(*slot, slot->lease, now_ms), slot->pending, slot->retiring, slot->owner != nullptr};
}
} // namespace lunaredu::lunaros
