#include "lunaredu_imu_service.h"

#include <cmath>
#include <utility>
#include "kernel/periodic_worker.h"
#include "imu_fusion_owner.h"

namespace lunaredu::services::imu {
namespace {

bool finite_sample(const ImuSample &sample) {
  for (float axis : sample.accel_g) {
    if (!std::isfinite(axis)) return false;
  }
  for (float axis : sample.gyro_dps) {
    if (!std::isfinite(axis)) return false;
  }
  return true;
}

}  // namespace
struct ImuService::StreamOwner {
  lunaros::KernelRuntime *runtime = nullptr;
  lunaros::ManagedTaskDefinition task;
  uint32_t period_us = 0;
  lunaros::PeriodicWorker worker;
};
ImuService::~ImuService() = default;
bool ImuService::configure_stream(lunaros::KernelRuntime &runtime,
    lunaros::ManagedTaskDefinition task, uint32_t period, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
  if (fusion_ && (fusion_->imu_owned || fusion_->magnetic_owned || fusion_->stopping)) {
    error = "imu_fusion_cleanup_pending"; return false;
  }
#endif
  if(status_.running || stream_ || !period || !task.id ||
      !task.owner_service_id || std::string(task.owner_service_id)!=kImuServiceId ||
      task.owner_kind!=lunaros::ManagedTaskOwnerKind::Service || task.execution.period_us!=period) {
    error="imu_stream_configuration_invalid";return false;
  }
  stream_=std::make_unique<StreamOwner>();
  stream_->runtime=&runtime;stream_->task=task;stream_->period_us=period;
  error.clear();return true;
}
bool ImuService::try_cached_sample(ImuSample &sample) const {
  if(!mutex_.try_lock()) return false;
  bool valid=status_.running && status_.sample_valid;
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
  if (fusion_ && fusion_->stopping) valid = false;
#endif
  if(valid) sample=latest_;
  mutex_.unlock();return valid;
}

ImuService::ImuService(ImuSource source, uint32_t cache_window_us)
    : source_(source), cache_window_us_(cache_window_us) {}

lunaros::ServiceNode ImuService::node(
    const char *owner_executor, lunaros::ServiceCriticality criticality) {
  return {{kImuServiceId, "Motion sensor", owner_executor, criticality},
          start_callback, stop_callback, this};
}

lunaros::ServiceStartResult ImuService::start_callback(void *context) {
  std::string error;
  const bool ready = static_cast<ImuService *>(context)->start(error);
  return {ready ? lunaros::ServiceStartDisposition::Ready
                : lunaros::ServiceStartDisposition::Failed,
          std::move(error)};
}

bool ImuService::stop_callback(void *context, std::string &error) {
  return static_cast<ImuService *>(context)->stop(error);
}

bool ImuService::start(std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error.clear();
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
  if (fusion_ && (fusion_->imu_owned || fusion_->magnetic_owned || fusion_->stopping)) {
    error = "imu_fusion_cleanup_pending"; return false;
  }
#endif
  if (status_.running) {
    error = "imu_already_running";
    return false;
  }
  if (!source_.start || !source_.read || !source_.stop || !source_.now_us) {
    error = "imu_source_incomplete";
    status_.last_error = error;
    return false;
  }
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
  if (fusion_) { fusion_->imu_owned = true; status_.sample_valid = false; }
#endif
  if (!source_.start(source_.context, error)) {
    if (error.empty()) error = "imu_source_start_failed";
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
    if (fusion_) {
      std::string cleanup;
      if (!stop_fusion_locked(cleanup) && !cleanup.empty()) error += ":" + cleanup;
    }
#endif
    status_.last_error = error;
    return false;
  }
  status_.running = true;
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
  if (!start_fusion_locked(error)) {
    std::string cleanup;
    if (!stop_fusion_locked(cleanup) && !cleanup.empty()) error += ":" + cleanup;
    status_.last_error = error; return false;
  }
#endif
  ImuSample probe;
  if (!read_locked(probe, error)) {
    std::string stop_error;
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
    if (fusion_) {
      if (!stop_fusion_locked(stop_error) && !stop_error.empty()) error += ":" + stop_error;
      status_.last_error = error; return false;
    }
#endif
    if (source_.stop(source_.context, stop_error)) {
      status_.running = false;
    } else if (!stop_error.empty()) {
      error += ":" + stop_error;
      status_.last_error = error;
    }
    return false;
  }
#ifdef ESP_PLATFORM
  if(stream_) {
    lunaros::PeriodicWorkerPorts ports;
    ports.context=this;
    ports.period_us=[](void *p){return static_cast<ImuService *>(p)->stream_->period_us;};
    ports.step=[](void *p,uint64_t,uint64_t){
      auto &self=*static_cast<ImuService *>(p);
      std::lock_guard<std::mutex> lock(self.mutex_);
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
      if (self.fusion_ && self.fusion_->stopping) return;
#endif
      if(self.status_.running) {ImuSample sample;std::string failure;(void)self.read_locked(sample,failure);}
    };
    if(!stream_->worker.start(*stream_->runtime,stream_->task,ports,error)) {
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
      if (fusion_) {
        fusion_->stopping = true; status_.sample_valid = false; invalidate_fusion_locked();
        std::string cleanup;
        if (stream_->worker.stop(cleanup)) (void)stop_fusion_locked(cleanup);
        else {
          fusion_->status.cleanup_pending = true;
          fusion_->cleanup_error = cleanup.empty() ? "imu_worker_cleanup_pending" : cleanup;
          refresh_fusion_error_locked();
        }
        if (!cleanup.empty()) error += ":" + cleanup;
        status_.last_error = error;
      }
#endif
      return false;
    }
  }
#else
  if(stream_) {
    error="imu_stream_requires_rtos";
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
    if (fusion_) {
      std::string cleanup;
      if (!stop_fusion_locked(cleanup) && !cleanup.empty()) error += ":" + cleanup;
      status_.last_error = error;
    }
#endif
    return false;
  }
#endif
  return true;
}

bool ImuService::stop(std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error.clear();
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
  if (fusion_ && (status_.running || fusion_->imu_owned || fusion_->magnetic_owned || fusion_->stopping)) {
    fusion_->stopping = true; status_.sample_valid = false; invalidate_fusion_locked();
#ifdef ESP_PLATFORM
    if (stream_ && !stream_->worker.stop(error)) {
      fusion_->status.cleanup_pending = true;
      fusion_->cleanup_error = error.empty() ? "imu_worker_cleanup_pending" : error;
      refresh_fusion_error_locked(); return false;
    }
#endif
    return stop_fusion_locked(error);
  }
#endif
  if (!status_.running) return true;
#ifdef ESP_PLATFORM
  if(stream_ && !stream_->worker.stop(error)) return false;
#endif
  if (!source_.stop(source_.context, error)) {
    if (error.empty()) error = "imu_source_stop_failed";
    status_.last_error = error;
    return false;
  }
  status_.running = false;
  status_.sample_valid = false;
  status_.last_error.clear();
  return true;
}

bool ImuService::read_locked(ImuSample &sample, std::string &error) {
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
  if (fusion_ && fusion_->stopping) { error = "imu_fusion_cleanup_pending"; return false; }
  // Read the slower optional sensor first so completion-time sensor stamps
  // normally precede the IMU sample. Filter still rejects future/old observations.
  read_magnetic_locked();
#endif
  ImuSample fresh;
  if (!source_.read(source_.context, fresh, error) || !finite_sample(fresh)) {
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
    invalidate_fusion_locked();
#endif
    if (error.empty()) error = "imu_source_read_failed";
    status_.sample_valid = false;
    ++status_.read_failures;
    status_.last_error = error;
    return false;
  }
  const auto completed_at=source_.now_us(source_.context);
  if(fresh.timestamp_us>completed_at) {
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
    invalidate_fusion_locked();
#endif
    error="imu_sample_time_in_future";status_.sample_valid=false;
    ++status_.read_failures;status_.last_error=error;return false;
  }
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
  const bool sampled_timestamp = fresh.timestamp_us != 0;
#endif
  if(!fresh.timestamp_us) fresh.timestamp_us=completed_at;
  fresh.sequence = ++status_.sample_count;
  latest_ = fresh;
  status_.sample_valid = true;
  status_.last_error.clear();
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
  if (fusion_) {
    if (sampled_timestamp) update_fusion_locked(fresh, completed_at);
    else {
      invalidate_fusion_locked();
      fusion_->attitude_error = "imu_fusion_requires_sample_timestamp";
      refresh_fusion_error_locked();
    }
  }
#endif
  sample = fresh;
  error.clear();
  return true;
}

bool ImuService::snapshot(ImuSample &sample, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error.clear();
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
  if (fusion_ && fusion_->stopping) { error = "imu_fusion_cleanup_pending"; return false; }
#endif
  if (!status_.running) {
    error = "imu_service_not_running";
    return false;
  }
  if(stream_) {
    if(!status_.sample_valid){error=status_.last_error;return false;}
    sample=latest_;return true;
  }
  const uint64_t now = source_.now_us(source_.context);
  if (status_.sample_valid && now >= latest_.timestamp_us &&
      now - latest_.timestamp_us < cache_window_us_) {
    sample = latest_;
    return true;
  }
  return read_locked(sample, error);
}

ImuStatus ImuService::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

}  // namespace lunaredu::services::imu
