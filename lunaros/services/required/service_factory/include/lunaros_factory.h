#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace lunaredu::lunaros {

// The factory catalogue belongs to LunarOS. A device or application owns the
// implementation of each check and registers it during product composition.
enum class FactoryCheckKind : uint8_t { System, Device, Application };
enum class FactoryCheckState : uint8_t {
  Unavailable, Ready, Running, AwaitingConfirmation, Passed, Failed
};
enum class FactoryStartState : uint8_t {
  Running, AwaitingConfirmation, Passed, Failed
};
enum class FactoryEntry : uint8_t { Usb, Web };

using FactoryAvailable = bool (*)(void *context, std::string &reason);
using FactoryStart = bool (*)(void *context, const char *action,
                             FactoryStartState &state, std::string &detail,
                             std::string &error);
using FactoryCancel = void (*)(void *context);
using FactoryConfirm = bool (*)(void *context, bool passed,
                                std::string &error);

struct FactoryCheckDefinition {
  const char *id = nullptr;
  const char *owner = nullptr;
  const char *label = nullptr;
  FactoryCheckKind kind = FactoryCheckKind::Device;
  FactoryAvailable available = nullptr;
  FactoryStart start = nullptr;
  FactoryCancel cancel = nullptr;
  FactoryConfirm confirm = nullptr;
  void *context = nullptr;
  const char *owner_label = nullptr;
};

struct FactoryCheckSnapshot {
  const char *id = nullptr;
  const char *owner = nullptr;
  const char *label = nullptr;
  FactoryCheckKind kind = FactoryCheckKind::Device;
  FactoryCheckState state = FactoryCheckState::Unavailable;
  std::string detail;
  uint32_t run_count = 0;
  const char *owner_label = nullptr;
};

inline constexpr size_t kFactoryCheckCapacity = 64;
struct FactorySnapshot {
  bool active = false;
  FactoryEntry entry = FactoryEntry::Usb;
  uint32_t revision = 0;
  size_t count = 0;
  std::array<FactoryCheckSnapshot, kFactoryCheckCapacity> checks{};
};

struct FactoryStatusSnapshot {
  bool active = false;
  FactoryEntry entry = FactoryEntry::Usb;
  uint32_t revision = 0;
  size_t count = 0;
  std::array<uint16_t, 6> state_counts{};
};

class LocalFactoryMode;
struct FactoryContributor {
  const char *owner = nullptr;
  FactoryCheckKind kind = FactoryCheckKind::Device;
  bool (*register_checks)(LocalFactoryMode &, std::string &) = nullptr;
};

// Registration and commands run on the product owner executor. Contributors
// post asynchronous completion back to that executor before calling finish.
class LocalFactoryMode {
 public:
  explicit LocalFactoryMode(bool available=true) : available_(available) {}
  void set_available(bool available) { available_=available;if(!available)leave(); }
  bool register_contributor(const FactoryContributor &contributor,
                            std::string &error);
  bool register_check(const FactoryCheckDefinition &check, std::string &error);
  void seal();
  bool enter(FactoryEntry entry, std::string &error);
  void leave();
  bool active() const { return snapshot_.active; }  // owner executor only
  FactoryStatusSnapshot status();
  bool check(const char *id, FactoryCheckSnapshot &out);
  FactorySnapshot snapshot();
  bool start(const char *id, const char *action, std::string &error);
  bool finish(const char *id, FactoryStartState state, const char *detail,
              std::string &error);
  bool confirm(const char *id, bool passed, std::string &error);
  bool reset(const char *id, std::string &error);

 private:
  size_t find_locked(const char *id) const;
  void refresh_locked(size_t index);
  bool active_locked() const { return snapshot_.active; }
  std::array<FactoryCheckDefinition, kFactoryCheckCapacity> definitions_{};
  FactorySnapshot snapshot_{};
  bool sealed_ = false;
  bool available_ = true;
};

LocalFactoryMode &local_factory_mode();

const char *factory_check_kind_name(FactoryCheckKind kind);
const char *factory_check_state_name(FactoryCheckState state);

}  // namespace lunaredu::lunaros
