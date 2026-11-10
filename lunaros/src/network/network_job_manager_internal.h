#pragma once

#include "kernel/static_mutex.h"
#include "lunaredu_lunaros/network/network_jobs.h"

namespace lunaredu::lunaros {

class NetworkJobManager {
public:
  using Clock = uint32_t (*)();
  explicit NetworkJobManager(Clock clock = nullptr) : clock_(clock) {}
  bool initialize(const NetworkJobPolicy &policy, std::string &error);
  void reset(const NetworkJobPolicy &policy = {});
  void set_wake_handler(NetworkJobWakeHandler handler, void *context);
  bool submit(const NetworkJobDefinition &definition, NetworkJobHandle &handle,
              std::string &error);
  bool cancel(NetworkJobHandle handle, std::string &error);
  bool cancellation_requested(NetworkJobHandle handle) const;
  bool active(NetworkJobHandle handle) const;
  bool begin_ota_maintenance(std::string &error);
  bool activate_ota_maintenance(std::string &error);
  void end_ota_maintenance();
  NetworkMaintenanceState maintenance_state() const;
  bool run_next(uint32_t now_ms);
  uint32_t next_wake_delay_ms(uint32_t now_ms) const;
  bool release(NetworkJobHandle handle, std::string &error);
  NetworkJobManagerSnapshot snapshot() const;

private:
  struct JobRecord {
    NetworkJobSnapshot snapshot;
    NetworkJobRunner runner = nullptr;
    void *context = nullptr;
    bool retain_terminal = false;
    uint32_t retry_backoff_initial_ms = 0;
    uint32_t retry_backoff_maximum_ms = 0;
  };

  int find_locked(NetworkJobHandle handle) const;
  int next_queued_locked(uint32_t now_ms);
  size_t active_count_locked() const;
  size_t owner_active_count_locked(const char *owner_service_id) const;
  bool job_runnable_locked(const NetworkJobSnapshot &job) const;
  bool ensure_job_storage(std::string &error);
  void reset_locked(const NetworkJobPolicy &policy);

  mutable StaticMutex mutex_;
  Clock clock_ = nullptr;
#ifdef ESP_PLATFORM
  JobRecord *jobs_ = nullptr;
#else
  std::array<JobRecord, kNetworkJobCapacity> owned_jobs_{};
  JobRecord *jobs_ = owned_jobs_.data();
#endif
  NetworkJobPolicy policy_{};
  NetworkJobMetrics metrics_{};
  uint32_t next_id_ = 1;
  uint32_t next_sequence_ = 1;
  NetworkMaintenanceState maintenance_state_ =
      NetworkMaintenanceState::Normal;
  NetworkJobWakeHandler wake_handler_ = nullptr;
  void *wake_context_ = nullptr;
};

NetworkJobManager &network_job_manager_internal();
bool setup_network_job_manager_internal(const NetworkJobPolicy &policy,
                                        std::string &error);
void reset_network_job_manager_internal(const NetworkJobPolicy &policy = {});

} // namespace lunaredu::lunaros

namespace lunaredu::lunaros::internal {

NetworkJobPolicy normalize_network_job_policy(NetworkJobPolicy policy);
bool valid_network_job_definition(const NetworkJobDefinition &definition);
bool network_job_terminal(NetworkJobState state);
bool network_job_time_reached(uint32_t now_ms, uint32_t target_ms);
uint32_t network_job_time_until(uint32_t now_ms, uint32_t target_ms);
uint16_t effective_network_job_priority(const NetworkJobSnapshot &job,
                                        const NetworkJobPolicy &policy);
uint32_t network_job_retry_backoff_ms(uint32_t initial_ms,
                                      uint32_t maximum_ms,
                                      uint8_t attempts);

} // namespace lunaredu::lunaros::internal
