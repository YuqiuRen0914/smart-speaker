#include "lunaros_factory.h"

#include <cstring>
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#include <sdkconfig.h>
#endif

namespace lunaredu::lunaros {
namespace {
constexpr size_t kMissing = kFactoryCheckCapacity;

FactoryCheckState to_state(FactoryStartState state) {
  switch (state) {
    case FactoryStartState::Running: return FactoryCheckState::Running;
    case FactoryStartState::AwaitingConfirmation:
      return FactoryCheckState::AwaitingConfirmation;
    case FactoryStartState::Passed: return FactoryCheckState::Passed;
    case FactoryStartState::Failed: return FactoryCheckState::Failed;
  }
  return FactoryCheckState::Failed;
}
}  // namespace

size_t LocalFactoryMode::find_locked(const char *id) const {
  if (id == nullptr) return kMissing;
  for (size_t i = 0; i < snapshot_.count; ++i)
    if (std::strcmp(definitions_[i].id, id) == 0) return i;
  return kMissing;
}

void LocalFactoryMode::refresh_locked(size_t index) {
  auto &item = snapshot_.checks[index];
  if (item.state == FactoryCheckState::Running ||
      item.state == FactoryCheckState::AwaitingConfirmation ||
      item.state == FactoryCheckState::Passed ||
      item.state == FactoryCheckState::Failed) return;
  std::string reason;
  const auto &check = definitions_[index];
  const bool available = check.available == nullptr ||
                         check.available(check.context, reason);
  const auto next = available ? FactoryCheckState::Ready
                              : FactoryCheckState::Unavailable;
  if (item.state != next || item.detail != reason) {
    item.state = next;
    item.detail = available ? std::string{} : std::move(reason);
    ++snapshot_.revision;
  }
}

bool LocalFactoryMode::register_check(const FactoryCheckDefinition &check,
                                      std::string &error) {
  if (sealed_ || snapshot_.active) { error = "factory_catalog_sealed"; return false; }
  if (check.id == nullptr || check.id[0] == '\0' || check.owner == nullptr ||
      check.owner[0] == '\0' || check.label == nullptr || check.start == nullptr) {
    error = "factory_check_invalid"; return false;
  }
  if (find_locked(check.id) != kMissing) { error = "factory_check_duplicate"; return false; }
  if (snapshot_.count == kFactoryCheckCapacity) {
    error = "factory_catalog_full"; return false;
  }
  const size_t index = snapshot_.count++;
  definitions_[index] = check;
  snapshot_.checks[index].id = check.id;
  snapshot_.checks[index].owner = check.owner;
  snapshot_.checks[index].label = check.label;
  snapshot_.checks[index].kind = check.kind;
  snapshot_.checks[index].owner_label = check.owner_label;
  ++snapshot_.revision;
  error.clear();
  return true;
}

bool LocalFactoryMode::register_contributor(
    const FactoryContributor &contributor, std::string &error) {
  if (sealed_ || snapshot_.active) {
    error = "factory_catalog_sealed";
    return false;
  }
  if (contributor.owner == nullptr || contributor.owner[0] == '\0' ||
      contributor.register_checks == nullptr) {
    error = "factory_contributor_invalid";
    return false;
  }
  const size_t first = snapshot_.count;
  const uint32_t previous_revision = snapshot_.revision;
  const bool registered = contributor.register_checks(*this, error);
  bool valid = registered && snapshot_.count > first;
  for (size_t i = first; valid && i < snapshot_.count; ++i)
    valid = definitions_[i].kind == contributor.kind &&
            std::strcmp(definitions_[i].owner, contributor.owner) == 0;
  if (!valid) {
    for (size_t i = first; i < snapshot_.count; ++i) {
      definitions_[i] = {};
      snapshot_.checks[i] = {};
    }
    snapshot_.count = first;
    snapshot_.revision = previous_revision;
    if (registered) error = "factory_contributor_mismatch_or_empty";
    return false;
  }
  error.clear();
  return true;
}

void LocalFactoryMode::seal() {
  sealed_ = true;
}

bool LocalFactoryMode::enter(FactoryEntry entry, std::string &error) {
  if (!available_) { error = "factory_service_unavailable"; return false; }
  if (!sealed_) { error = "factory_catalog_not_sealed"; return false; }
  if (snapshot_.active) {
    if (snapshot_.entry != entry) {
      error = snapshot_.entry == FactoryEntry::Usb ? "factory_mode_owned_by_usb" : "factory_mode_owned_by_web";
      return false;
    }
    error.clear();
    return true;
  }
  snapshot_.active = true;
  snapshot_.entry = entry;
  for (size_t i = 0; i < snapshot_.count; ++i) refresh_locked(i);
  ++snapshot_.revision;
  error.clear();
  return true;
}

void LocalFactoryMode::leave() {
  FactoryCancel cancel = nullptr;
  void *context = nullptr;
  {
      if (!snapshot_.active) return;
    for (size_t i = 0; i < snapshot_.count; ++i) {
      if (snapshot_.checks[i].state == FactoryCheckState::Running ||
          snapshot_.checks[i].state == FactoryCheckState::AwaitingConfirmation) {
        cancel = definitions_[i].cancel;
        context = definitions_[i].context;
        snapshot_.checks[i].state = FactoryCheckState::Ready;
        snapshot_.checks[i].detail = "cancelled_on_exit";
      }
    }
    snapshot_.active = false;
    ++snapshot_.revision;
  }
  if (cancel != nullptr) cancel(context);
}

FactorySnapshot LocalFactoryMode::snapshot() {
  for (size_t i = 0; i < snapshot_.count; ++i) refresh_locked(i);
  return snapshot_;
}

FactoryStatusSnapshot LocalFactoryMode::status() {
  for (size_t i = 0; i < snapshot_.count; ++i) refresh_locked(i);
  FactoryStatusSnapshot result;
  result.active = snapshot_.active;
  result.entry = snapshot_.entry;
  result.revision = snapshot_.revision;
  result.count = snapshot_.count;
  for (size_t i = 0; i < snapshot_.count; ++i)
    ++result.state_counts[static_cast<size_t>(snapshot_.checks[i].state)];
  return result;
}

bool LocalFactoryMode::check(const char *id, FactoryCheckSnapshot &out) {
  const size_t index = find_locked(id);
  if (index == kMissing) return false;
  refresh_locked(index);
  out = snapshot_.checks[index];
  return true;
}

bool LocalFactoryMode::start(const char *id, const char *action,
                             std::string &error) {
  FactoryCheckDefinition check;
  size_t index;
  {
      if (!active_locked()) { error = "factory_mode_inactive"; return false; }
    index = find_locked(id);
    if (index == kMissing) { error = "factory_check_unknown"; return false; }
    for (size_t i = 0; i < snapshot_.count; ++i)
      if (snapshot_.checks[i].state == FactoryCheckState::Running ||
          snapshot_.checks[i].state == FactoryCheckState::AwaitingConfirmation) {
        error = "factory_check_busy"; return false;
      }
    refresh_locked(index);
    if (snapshot_.checks[index].state == FactoryCheckState::Unavailable) {
      error = "factory_check_unavailable"; return false;
    }
    check = definitions_[index];
    snapshot_.checks[index].state = FactoryCheckState::Running;
    snapshot_.checks[index].detail = "running";
    ++snapshot_.checks[index].run_count;
    ++snapshot_.revision;
  }
  FactoryStartState next = FactoryStartState::Running;
  std::string detail;
  const bool ok = check.start(check.context, action == nullptr ? "" : action,
                              next, detail, error);
  {
      if (snapshot_.active && snapshot_.checks[index].state == FactoryCheckState::Running) {
      snapshot_.checks[index].state = ok ? to_state(next) : FactoryCheckState::Failed;
      snapshot_.checks[index].detail = ok ? std::move(detail) : error;
      ++snapshot_.revision;
    }
  }
  if (ok) error.clear();
  return ok;
}

bool LocalFactoryMode::finish(const char *id, FactoryStartState state,
                              const char *detail, std::string &error) {
  const size_t index = find_locked(id);
  if (!snapshot_.active || index == kMissing ||
      snapshot_.checks[index].state != FactoryCheckState::Running ||
      state == FactoryStartState::Running) {
    error = "factory_check_not_running"; return false;
  }
  snapshot_.checks[index].state = to_state(state);
  snapshot_.checks[index].detail = detail == nullptr ? "" : detail;
  ++snapshot_.revision;
  error.clear();
  return true;
}

bool LocalFactoryMode::confirm(const char *id, bool passed,
                               std::string &error) {
  const size_t index = find_locked(id);
  if (!snapshot_.active || index == kMissing ||
      snapshot_.checks[index].state != FactoryCheckState::AwaitingConfirmation) {
    error = "factory_check_not_awaiting_confirmation"; return false;
  }
  const auto &check = definitions_[index];
  if (check.confirm != nullptr && !check.confirm(check.context, passed, error))
    return false;
  snapshot_.checks[index].state = passed ? FactoryCheckState::Passed
                                         : FactoryCheckState::Failed;
  snapshot_.checks[index].detail = passed ? "operator_confirmed_pass"
                                           : "operator_confirmed_fail";
  ++snapshot_.revision;
  error.clear();
  return true;
}

bool LocalFactoryMode::reset(const char *id, std::string &error) {
  const size_t index = find_locked(id);
  if (!snapshot_.active || index == kMissing ||
      snapshot_.checks[index].state == FactoryCheckState::Running) {
    error = "factory_check_reset_rejected"; return false;
  }
  if (snapshot_.checks[index].state == FactoryCheckState::AwaitingConfirmation &&
      definitions_[index].cancel != nullptr)
    definitions_[index].cancel(definitions_[index].context);
  snapshot_.checks[index].state = FactoryCheckState::Ready;
  snapshot_.checks[index].detail.clear();
  ++snapshot_.revision;
  error.clear();
  return true;
}

LocalFactoryMode &local_factory_mode() {
#if defined(ESP_PLATFORM) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
  // Normal-task inspection/configuration state; never accessed by an ISR or
  // while Flash caches are disabled. Hardware callbacks own their own buffers.
  static EXT_RAM_BSS_ATTR LocalFactoryMode instance(false);
#else
  static LocalFactoryMode instance(false);
#endif
  return instance;
}

const char *factory_check_kind_name(FactoryCheckKind kind) {
  switch (kind) {
    case FactoryCheckKind::System: return "system";
    case FactoryCheckKind::Device: return "device";
    case FactoryCheckKind::Application: return "application";
  }
  return "unknown";
}

const char *factory_check_state_name(FactoryCheckState state) {
  switch (state) {
    case FactoryCheckState::Unavailable: return "unavailable";
    case FactoryCheckState::Ready: return "ready";
    case FactoryCheckState::Running: return "running";
    case FactoryCheckState::AwaitingConfirmation: return "awaiting_confirmation";
    case FactoryCheckState::Passed: return "passed";
    case FactoryCheckState::Failed: return "failed";
  }
  return "unknown";
}

}  // namespace lunaredu::lunaros
