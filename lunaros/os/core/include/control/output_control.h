#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace lunaredu::lunaros {

// Device-scoped control authority. The caller is supplied by trusted firmware
// ingress, never read from an external command body. Hardware providers retain
// their own stop/recovery behavior, but do not grant control independently.
// Declaration strings must remain valid for the lifetime of the device.
class OutputControl {
public:
  static constexpr size_t kOutputCapacity = 16;
  static constexpr size_t kConsumerCapacity = 48;

  struct Provider {
    const char *output = nullptr;
    const char *id = nullptr;
    uint32_t maximum_lease_ms = 0; // 0: long-lived until explicit release.
    bool preemptible = false;       // Provider confirms old work has stopped.
  };
  struct Consumer {
    const char *caller = nullptr;
    const char *output = nullptr;
    uint8_t priority = 0;
  };
  struct Lease {
    uint64_t id = 0;
    const char *output = nullptr;
    const char *caller = nullptr;
    uint32_t deadline_ms = 0;
    uint8_t priority = 0;
  };

  bool register_provider(Provider provider, std::string &error);
  bool register_consumer(Consumer consumer, std::string &error);
  bool seal(std::string &error);
  // Refuses to discard active or pending provider ownership.
  bool reset(std::string &error);
  // Preflight for a compound composition reset; caller serializes composition.
  bool can_reset(std::string &error);
  bool ready(const char *output, const char *provider);
  // Exclusive borrowed provider lifetime. Prevents composition reset while
  // an idle physical service still owns callbacks/resources. No callbacks
  // execute under this registry's mutex. Owner remains valid until detach.
  bool attach_provider(const char *output, const char *provider, const void *owner, std::string &error);
  bool detach_provider(const char *output, const char *provider, const void *owner, std::string &error);

  struct Admission { Lease requested; Lease retiring; };
  // Admission closes use of both leases. Only the registered provider may
  // confirm after draining/stopping old work, or cancel after safe recovery.
  bool admit(const char *output, const char *caller, uint32_t lease_ms,
               uint32_t now_ms, uint64_t replace_id, Admission &admission,
               std::string &error);
  bool renew(uint64_t id, const char *caller, uint32_t lease_ms,
             uint32_t now_ms, std::string &error);
  bool confirm(uint64_t admission_id, const char *provider, uint32_t now_ms,
               Lease &granted, std::string &error);
  bool cancel(uint64_t admission_id, const char *provider, std::string &error);
  // Provider identity comes from trusted firmware composition. begin_retirement
  // closes admission before stopping work; retire acknowledges completion.
  // Expiration denies access but retains ownership until that acknowledgement.
  bool begin_retirement(uint64_t id, const char *provider, std::string &error);
  bool retire(uint64_t id, const char *provider, std::string &error);
  bool holds(uint64_t id, const char *caller, uint32_t now_ms);
  Lease snapshot(const char *output, uint32_t now_ms);
  struct Observation { Provider provider; Lease lease; bool expired = false; Lease pending; bool retiring = false; bool provider_attached = false; };
  // Observes without expiring/revoking leases or changing hardware state.
  size_t observe(Observation *output, size_t capacity, uint32_t now_ms);
  // A single output observation avoids allocating the whole registry on a
  // control task's small stack. It never clears an expired lease.
  Observation observe(const char *output, uint32_t now_ms);


private:
  struct Slot {
    Provider provider;
    Lease lease;
    Lease pending;
    bool retiring = false;
    const void *owner = nullptr;
  };
  Slot *find_output_locked(const char *output);
  const Consumer *find_consumer_locked(const char *output,
                                       const char *caller) const;
  static bool expired(const Slot &slot, const Lease &lease, uint32_t now_ms);
  bool can_reset_locked(std::string &error) const;
  std::mutex mutex_;
  std::array<Slot, kOutputCapacity> outputs_{};
  std::array<Consumer, kConsumerCapacity> consumers_{};
  size_t output_count_ = 0;
  size_t consumer_count_ = 0;
  uint64_t generation_ = 0;
  bool sealed_ = false;
};

} // namespace lunaredu::lunaros
