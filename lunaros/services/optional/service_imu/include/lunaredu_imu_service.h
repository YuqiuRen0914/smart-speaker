#pragma once

#include "kernel/service.h"
#include "kernel/managed_task.h"
#include "lunaredu_imu_fusion.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <memory>

namespace lunaredu::services::imu {

inline constexpr char kImuServiceId[] = "imu.motion";

struct ImuSample {
  std::array<float, 3> accel_g{};
  std::array<float, 3> gyro_dps{};
  uint64_t timestamp_us = 0;
  uint32_t sequence = 0;
};

// The product owns the driver context and maps its coordinates and units.
// Callbacks must be bounded and must not reenter ImuService. All four callbacks
// are required.
struct ImuSource {
  void *context = nullptr;
  bool (*start)(void *context, std::string &error) = nullptr;
  bool (*read)(void *context, ImuSample &sample, std::string &error) = nullptr;
  bool (*stop)(void *context, std::string &error) = nullptr;
  uint64_t (*now_us)(void *context) = nullptr;
};

struct ImuStatus {
  bool running = false;
  bool sample_valid = false;
  uint32_t sample_count = 0;
  uint32_t read_failures = 0;
  std::string last_error;
};

enum class MagneticReadResult { Fresh, NotReady, Error, Overflow };
struct MagneticSample {
  std::array<float, 3> field_ut{};
  uint64_t timestamp_us = 0;
  uint32_t sequence = 0;
};
// Optional caller-owned source in the same body coordinates and monotonic clock
// domain as ImuSource. Bounded callbacks cannot reenter this service. A failed
// start may have partially acquired resources: stop must clean them and permit
// retries. Fresh means a new sensor sample; NotReady must not relabel old data.
// Keep the context alive until the service has successfully stopped.
struct MagneticSource {
  void *context = nullptr;
  bool (*start)(void *, std::string &) = nullptr;
  MagneticReadResult (*read)(void *, MagneticSample &, std::string &) = nullptr;
  bool (*stop)(void *, std::string &) = nullptr;
};
struct FusionStatus {
  bool compiled = false;
  bool configured = false;
  bool magnetic_running = false;
  bool cleanup_pending = false;
  uint32_t read_failures = 0;
  std::string last_error;
};

class ImuService {
 public:
  explicit ImuService(ImuSource source, uint32_t cache_window_us = 5000);
  ImuService(const ImuService &) = delete;
  ImuService &operator=(const ImuService &) = delete;
  ~ImuService();
  // Optional OS-managed acquisition. Configure before lifecycle start; all
  // hardware reads then belong to this worker, while Apps only read snapshots.
  bool configure_stream(lunaros::KernelRuntime &, lunaros::ManagedTaskDefinition,
                        uint32_t period_us, std::string &error);
  // Nonblocking copy, no hardware I/O or error-string allocation. Consumers
  // must check timestamp freshness. False includes short publication contention.
  bool try_cached_sample(ImuSample &) const;

  // Opt in before lifecycle start. Without this call the original raw IMU
  // path is unchanged, even when fusion is compiled. An empty magnetic source
  // enables six-axis fusion only. Hardware sampling remains on the existing
  // acquisition owner; cached fusion APIs never read a sensor or run fusion.
  // Consumers must check Output.timestamp_us freshness against their own clock.
  bool configure_fusion(const fusion::Config &, MagneticSource, std::string &error,
                        const fusion::Quaternion &initial = {{1, 0, 0, 0}});
  bool snapshot_fusion(fusion::Output &, std::string &error) const;
  bool try_cached_fusion(fusion::Output &) const;
  FusionStatus fusion_status() const;

  // The returned node keeps a pointer to this instance. Keep it alive through
  // Kernel stop and all App consumers.
  lunaros::ServiceNode node(
      const char *owner_executor,
      lunaros::ServiceCriticality criticality =
          lunaros::ServiceCriticality::Optional);

  bool snapshot(ImuSample &sample, std::string &error);
  ImuStatus status() const;

 private:
  static lunaros::ServiceStartResult start_callback(void *context);
  static bool stop_callback(void *context, std::string &error);
  bool start(std::string &error);
  bool stop(std::string &error);
  bool read_locked(ImuSample &sample, std::string &error);
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
  struct FusionOwner;
  std::unique_ptr<FusionOwner> fusion_;
  bool start_fusion_locked(std::string &error);
  bool stop_fusion_locked(std::string &error);
  void update_fusion_locked(const ImuSample &, uint64_t completed_at);
  void read_magnetic_locked();
  void refresh_fusion_error_locked();
  void invalidate_fusion_locked();
#endif

  const ImuSource source_;
  const uint32_t cache_window_us_;
  mutable std::mutex mutex_;
  ImuStatus status_;
  ImuSample latest_;
  struct StreamOwner;
  std::unique_ptr<StreamOwner> stream_;
};

}  // namespace lunaredu::services::imu
