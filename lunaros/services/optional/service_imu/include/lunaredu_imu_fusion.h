#pragma once
#include <array>
#include <cstdint>

namespace lunaredu::services::imu::fusion {
using Vector3 = std::array<float,3>;
// Right-handed body -> world rotation; world +Z is up. Scalar first: w,x,y,z.
using Quaternion = std::array<float,4>;
enum class Mode : uint8_t { Stopped, SixAxis, NineAxis };
enum class MagneticQuality : uint8_t {
  Unavailable, Uncalibrated, FreshClean, Recovering, Duplicate, Stale, Future,
  Invalid, Overflow, Saturated, StrongField, MagnitudeMismatch,
  HeadingInnovation, InsufficientHorizontalField
};
enum class UpdateCode : uint8_t {
  Updated, Primed, Stopped, InvalidImu, NonMonotonicImu, ImuGap, NumericalFailure
};
struct Config {
  // Startup only: first near-1g sample sets tilt while retaining initial yaw.
  // Never re-runs after invalidation, gaps, or magnetic rejection/recovery.
  bool initialize_tilt_from_accel = true;
  float accel_gain = 2.0F;
  float magnetic_gain = 2.0F;
  float minimum_accel_g = 0.75F, maximum_accel_g = 1.25F;
  float maximum_input_accel_g = 1000.0F, maximum_gyro_dps = 4000.0F;
  Vector3 gyro_bias_dps{};
  Vector3 hard_iron_uT{};
  std::array<float,9> soft_iron_matrix{{1,0,0,0,1,0,0,0,1}}; // Row-major.
  // Explicit clean reference, never estimated/learned at startup. Zero disables
  // magnetic heading use (six-axis still works and strong fields are detected).
  float expected_field_uT = 0.0F;
  float field_tolerance_fraction = 0.25F;
  float strong_field_uT = 120.0F;
  float maximum_heading_innovation_rad = 0.610865238F; // 35 degrees.
  float reference_heading_rad = 0.0F; // World magnetic horizontal heading.
  float minimum_horizontal_fraction = 0.10F;
  uint32_t maximum_imu_gap_us = 100000;
  uint32_t maximum_magnetic_age_us = 100000;
  uint32_t recovery_good_us = 500000;
  uint32_t recovery_ramp_us = 1000000;
  uint16_t recovery_minimum_samples = 3;
};
struct ImuInput {
  Vector3 accel_g{};
  Vector3 gyro_dps{};
  uint64_t timestamp_us = 0; // Same monotonic time domain as magnetometer.
  bool valid = false;
};
struct MagneticInput {
  Vector3 field_uT{}; // Caller maps all sensors into the same body frame first.
  uint64_t timestamp_us = 0;
  uint32_t sequence = 0;
  bool valid = false, overflow = false, saturated = false;
};
struct Output {
  Quaternion quaternion{{1,0,0,0}};
  Vector3 euler_rad{}; // Roll, pitch, yaw; Euler yaw wraps at +/-pi.
  Vector3 gravity_g{{0,0,1}}; // Predicted gravity direction in body coordinates.
  Vector3 linear_accel_g{}; // Input acceleration minus predicted gravity.
  uint64_t timestamp_us = 0;
  uint32_t sequence = 0;
  bool valid = false;
  Mode mode = Mode::Stopped;
  MagneticQuality magnetic_quality = MagneticQuality::Unavailable;
  float magnetic_weight = 0;
  float calibrated_field_uT = 0;
  float heading_innovation_rad = 0;
  uint64_t magnetic_age_us = 0;
  bool magnetic_sample_fresh = false, magnetic_correction_applied = false;
  bool accel_correction_applied = false;
  // Latched suspicion, NOT toy identity. Invalid data or actual age expiry make
  // presence unknown. nullptr means NotReady: retain the last observed presence
  // within maximum_magnetic_age_us, without applying old-body correction.
  // None asserts that the magnet was removed. Stable clean fresh
  // observations clear suspicion. event_sequence increments only on transitions.
  bool suspected_magnetic_proximity = false, magnetic_presence_valid = false;
  uint32_t proximity_event_sequence = 0;
};
// Portable, allocation-free and caller-serialized. No task, lock, sensor I/O or
// automatic calibration. Persistent magnetic disturbance NEVER times out into
// acceptance. Reference and initial heading must come from a trusted condition.
class Filter {
 public:
  bool start(const Config &, const Quaternion &initial = {{1,0,0,0}});
  void stop();
  bool reset_orientation(const Quaternion &); // Explicit; resets timing/trust.
  void invalidate_sample(); // Keeps quaternion and suspicion; no fake dt on resume.
  UpdateCode update(const ImuInput &, const MagneticInput *magnetic = nullptr);
  Output snapshot() const { return output_; }
  bool running() const { return running_; }
 private:
  void clear_magnetic_trust(MagneticQuality, bool presence_known = false);
  void suspect(MagneticQuality);
  void project(const Vector3 &accel);
  bool magnetic_update(const MagneticInput *, uint64_t now, const Quaternion &predicted, Vector3 &correction);
  Config config_{};
  Output output_{};
  bool running_ = false, have_imu_time_ = false, have_mag_seen_ = false;
  bool tilt_initialized_ = false;
  bool magnetic_trusted_ = false, have_clean_streak_ = false;
  uint64_t last_imu_us_ = 0, last_mag_us_ = 0, clean_streak_start_us_ = 0;
  uint64_t last_clean_mag_us_ = 0, ramp_start_us_ = 0;
  uint32_t last_mag_sequence_ = 0;
  uint16_t clean_samples_ = 0;
};
} // namespace lunaredu::services::imu::fusion
