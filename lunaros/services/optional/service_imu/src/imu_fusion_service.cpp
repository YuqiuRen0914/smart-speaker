#include "imu_fusion_owner.h"
#include <utility>
#include <cmath>

namespace lunaredu::services::imu {
#if defined(CONFIG_LUNAROS_IMU_FUSION) && CONFIG_LUNAROS_IMU_FUSION
bool ImuService::configure_fusion(const fusion::Config &config, MagneticSource source,
    std::string &error, const fusion::Quaternion &initial) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.running || (fusion_ && (fusion_->imu_owned ||
      fusion_->magnetic_owned || fusion_->stopping))) {
    error = "imu_fusion_configuration_busy"; return false;
  }
  const bool any_source = source.context || source.start || source.read || source.stop;
  if (any_source && (!source.start || !source.read || !source.stop)) {
    error = "imu_magnetic_source_incomplete"; return false;
  }
  auto candidate = std::make_unique<FusionOwner>();
  if (!candidate->filter.start(config, initial)) {
    error = "imu_fusion_configuration_invalid"; return false;
  }
  candidate->filter.stop();
  candidate->config = config; candidate->initial = initial; candidate->source = source;
  candidate->status.compiled = candidate->status.configured = true;
  fusion_ = std::move(candidate);
  error.clear(); return true;
}
bool ImuService::start_fusion_locked(std::string &error) {
  if (!fusion_) return true;
  auto &owner = *fusion_;
  owner.magnetic_count = 0; owner.magnetic_fault_pending = false;
  owner.magnetic_error.clear(); owner.attitude_error.clear(); owner.cleanup_error.clear();
  owner.status = {}; owner.status.compiled = owner.status.configured = true;
  if (!owner.filter.start(owner.config, owner.initial)) {
    error = "imu_fusion_start_failed"; owner.attitude_error = error;
    refresh_fusion_error_locked(); return false;
  }
  if (owner.source.start) {
    owner.magnetic_owned = true; // A false start may have partially acquired hardware.
    std::string magnetic_error;
    if (owner.source.start(owner.source.context, magnetic_error)) {
      owner.status.magnetic_running = true;
    } else {
      owner.magnetic_error = magnetic_error.empty() ?
          "imu_magnetic_source_start_failed" : magnetic_error;
      std::string stop_error;
      if (owner.source.stop(owner.source.context, stop_error)) owner.magnetic_owned = false;
      else {
        owner.status.cleanup_pending = true;
        owner.cleanup_error = stop_error.empty() ? "imu_magnetic_source_stop_failed" : stop_error;
      }
      // A magnetic failure does not take down a valid raw IMU or six-axis attitude.
    }
  }
  refresh_fusion_error_locked();
  return true;
}
void ImuService::refresh_fusion_error_locked() {
  auto &owner = *fusion_;
  owner.status.last_error = !owner.cleanup_error.empty() ? owner.cleanup_error :
      !owner.magnetic_error.empty() ? owner.magnetic_error : owner.attitude_error;
}
bool ImuService::stop_fusion_locked(std::string &error) {
  auto &owner = *fusion_;
  owner.stopping = true;
  owner.filter.invalidate_sample(); owner.magnetic_count = 0; owner.magnetic_fault_pending = false;
  if (owner.magnetic_owned) {
    if (!owner.source.stop(owner.source.context, error)) {
      if (error.empty()) error = "imu_magnetic_source_stop_failed";
      owner.status.cleanup_pending = true; owner.cleanup_error = error;
      refresh_fusion_error_locked();
      return false;
    }
    owner.magnetic_owned = false; owner.status.magnetic_running = false;
  }
  if (owner.imu_owned) {
    if (!source_.stop(source_.context, error)) {
      if (error.empty()) error = "imu_source_stop_failed";
      owner.status.cleanup_pending = true; owner.cleanup_error = error;
      refresh_fusion_error_locked();
      status_.last_error = error; return false;
    }
    owner.imu_owned = false;
  }
  owner.filter.stop(); owner.stopping = false;
  owner.status.cleanup_pending = false; owner.status.last_error.clear();
  owner.magnetic_error.clear(); owner.attitude_error.clear(); owner.cleanup_error.clear();
  status_.running = status_.sample_valid = false; status_.last_error.clear();
  error.clear(); return true;
}
void ImuService::read_magnetic_locked() {
  if (!fusion_ || !fusion_->status.magnetic_running || fusion_->stopping) return;
  auto &owner = *fusion_;
  MagneticSample sample;
  std::string error;
  const auto result = owner.source.read(owner.source.context, sample, error);
  if (result == MagneticReadResult::NotReady) return;
  fusion::MagneticInput input{};
  input.timestamp_us = sample.timestamp_us; input.sequence = sample.sequence;
  bool fault = result != MagneticReadResult::Fresh;
  if (result == MagneticReadResult::Fresh) {
    input.field_uT = sample.field_ut; input.valid = true;
    // A timestamp must describe the sensor sample, not a relabelled cache.
    const auto completed = source_.now_us(source_.context);
    if (!sample.timestamp_us || sample.timestamp_us > completed) {
      // Deliver a future timestamp once for a precise Future diagnostic, never
      // queue it for later acceptance merely because the clock caught up.
      fault = true;
      ++owner.status.read_failures;
      owner.magnetic_error = "imu_magnetic_sample_time_invalid";
    } else {
      for (float axis : sample.field_ut) if (!std::isfinite(axis)) fault = true;
      if (fault) {
        input.valid = false; ++owner.status.read_failures;
        owner.magnetic_error = "imu_magnetic_sample_invalid";
      } else owner.magnetic_error.clear();
    }
  } else {
    input.overflow = result == MagneticReadResult::Overflow;
    // Overflow is a newly observed status, not a valid field sample. A driver
    // may clear its unusable sample on overflow, so time this diagnostic read.
    if (input.overflow && !input.timestamp_us) input.timestamp_us = source_.now_us(source_.context);
    ++owner.status.read_failures;
    owner.magnetic_error = error.empty() ? "imu_magnetic_source_read_failed" : error;
  }
  if (!fault && owner.magnetic_count == owner.magnetic_queue.size()) {
    input.valid = false; fault = true;
    ++owner.status.read_failures; owner.magnetic_error = "imu_magnetic_alignment_queue_full";
  }
  if (fault) {
    owner.magnetic_count = 0; owner.magnetic_fault = input; owner.magnetic_fault_pending = true;
  } else {
    owner.magnetic_queue[owner.magnetic_count++] = input;
  }
  refresh_fusion_error_locked();
}
void ImuService::update_fusion_locked(const ImuSample &sample, uint64_t completed_at) {
  if (!fusion_ || fusion_->stopping) return;
  auto &owner = *fusion_;
  if (!sample.timestamp_us || completed_at - sample.timestamp_us > owner.config.maximum_imu_gap_us) {
    owner.filter.invalidate_sample();
    owner.attitude_error = "imu_fusion_sample_stale";
    refresh_fusion_error_locked(); return;
  }
  const fusion::ImuInput input{sample.accel_g, sample.gyro_dps, sample.timestamp_us, true};
  fusion::MagneticInput aligned;
  const fusion::MagneticInput *magnetic = nullptr;
  if (owner.magnetic_fault_pending) {
    aligned = owner.magnetic_fault; magnetic = &aligned;
    owner.magnetic_fault_pending = false;
  } else {
    size_t retained = 0;
    for (size_t i = 0; i < owner.magnetic_count; ++i) {
      const auto &candidate = owner.magnetic_queue[i];
      if (candidate.timestamp_us <= sample.timestamp_us) {
        if (!magnetic || candidate.timestamp_us > aligned.timestamp_us) {
          aligned = candidate; magnetic = &aligned;
        }
      } else owner.magnetic_queue[retained++] = candidate;
    }
    owner.magnetic_count = retained;
  }
  const auto result = owner.filter.update(input, magnetic);
  if (result == fusion::UpdateCode::InvalidImu || result == fusion::UpdateCode::NonMonotonicImu ||
      result == fusion::UpdateCode::ImuGap) owner.attitude_error = "imu_fusion_sample_time_invalid";
  else if (result == fusion::UpdateCode::NumericalFailure)
    owner.attitude_error = "imu_fusion_numerical_failure";
  else if (result == fusion::UpdateCode::Updated || result == fusion::UpdateCode::Primed)
    owner.attitude_error.clear();
  refresh_fusion_error_locked();
}
void ImuService::invalidate_fusion_locked() {
  if (fusion_) {
    fusion_->filter.invalidate_sample(); fusion_->magnetic_count = 0;
    fusion_->magnetic_fault_pending = false;
  }
}
bool ImuService::snapshot_fusion(fusion::Output &out, std::string &error) const {
  std::lock_guard<std::mutex> lock(mutex_);
  out = {};
  if (!fusion_) { error = "imu_fusion_not_configured"; return false; }
  if (!status_.running || fusion_->stopping) { error = "imu_fusion_not_running"; return false; }
  out = fusion_->filter.snapshot();
  if (!out.valid || !status_.sample_valid) {
    out.valid = false; error = "imu_fusion_sample_invalid"; return false;
  }
  error.clear(); return true;
}
bool ImuService::try_cached_fusion(fusion::Output &out) const {
  out = {};
  if (!mutex_.try_lock()) return false;
  const bool available = fusion_ && status_.running && status_.sample_valid && !fusion_->stopping;
  if (available) out = fusion_->filter.snapshot();
  mutex_.unlock(); return available && out.valid;
}
FusionStatus ImuService::fusion_status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (fusion_) return fusion_->status;
  FusionStatus result; result.compiled = true; return result;
}
#else
bool ImuService::configure_fusion(const fusion::Config &, MagneticSource, std::string &error,
                                const fusion::Quaternion &) {
  error = "imu_fusion_not_compiled"; return false;
}
bool ImuService::snapshot_fusion(fusion::Output &out, std::string &error) const {
  out = {}; error = "imu_fusion_not_compiled"; return false;
}
bool ImuService::try_cached_fusion(fusion::Output &out) const { out = {}; return false; }
FusionStatus ImuService::fusion_status() const { return {}; }
#endif
} // namespace lunaredu::services::imu
