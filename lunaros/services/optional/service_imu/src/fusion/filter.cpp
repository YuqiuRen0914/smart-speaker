#include "lunaredu_imu_fusion.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace lunaredu::services::imu::fusion {
namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kDegreesToRadians = kPi / 180.0F;
bool finite(const Vector3 &v) {
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
double norm(const Vector3 &v) {
  return std::sqrt(double(v[0])*v[0] + double(v[1])*v[1] + double(v[2])*v[2]);
}
Vector3 normalized(const Vector3 &v, double length) {
  return {{float(v[0]/length),float(v[1]/length),float(v[2]/length)}};
}
Vector3 cross(const Vector3 &a, const Vector3 &b) {
  return {{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}};
}
Quaternion multiply(const Quaternion &a, const Quaternion &b) {
  return {{a[0]*b[0]-a[1]*b[1]-a[2]*b[2]-a[3]*b[3],
    a[0]*b[1]+a[1]*b[0]+a[2]*b[3]-a[3]*b[2],
    a[0]*b[2]-a[1]*b[3]+a[2]*b[0]+a[3]*b[1],
    a[0]*b[3]+a[1]*b[2]-a[2]*b[1]+a[3]*b[0]}};
}
bool normalize_quaternion(Quaternion &q) {
  double length2 = 0;
  for (float value : q) {
    if (!std::isfinite(value)) return false;
    length2 += double(value)*value;
  }
  if (!std::isfinite(length2) || length2 < 1e-12) return false;
  const double length = std::sqrt(length2);
  for (auto &value : q) value = float(value/length);
  return true;
}
Quaternion rotate_step(const Quaternion &q, const Vector3 &rotation) {
  const double angle = norm(rotation);
  const double half = 0.5*angle;
  const double scale = angle < 1e-8 ? 0.5 : std::sin(half)/angle;
  Quaternion step{{float(std::cos(half)),float(rotation[0]*scale),
                   float(rotation[1]*scale),float(rotation[2]*scale)}};
  auto result = multiply(q,step);
  (void)normalize_quaternion(result);
  return result;
}
Vector3 body_gravity(const Quaternion &q) {
  return {{2*(q[1]*q[3]-q[0]*q[2]),2*(q[2]*q[3]+q[0]*q[1]),
           1-2*(q[1]*q[1]+q[2]*q[2])}};
}
Vector3 world_vector(const Quaternion &q, const Vector3 &body) {
  const Vector3 axis{{q[1],q[2],q[3]}};
  const auto first = cross(axis,body);
  const auto second = cross(axis,first);
  return {{body[0]+2*q[0]*first[0]+2*second[0],
           body[1]+2*q[0]*first[1]+2*second[1],
           body[2]+2*q[0]*first[2]+2*second[2]}};
}
bool valid_config(const Config &c) {
  const float scalars[] = {c.accel_gain,c.magnetic_gain,c.minimum_accel_g,c.maximum_accel_g,
    c.maximum_input_accel_g,c.maximum_gyro_dps,c.expected_field_uT,c.field_tolerance_fraction,
    c.strong_field_uT,c.maximum_heading_innovation_rad,c.reference_heading_rad,c.minimum_horizontal_fraction};
  for (float value : scalars) if (!std::isfinite(value)) return false;
  if (c.accel_gain<0 || c.accel_gain>100 || c.magnetic_gain<0 || c.magnetic_gain>100 ||
      c.minimum_accel_g<=0 || c.maximum_accel_g<=c.minimum_accel_g ||
      c.maximum_accel_g>100 || c.maximum_input_accel_g<c.maximum_accel_g ||
      c.maximum_input_accel_g>1000000 || c.maximum_gyro_dps<=0 || c.maximum_gyro_dps>1000000 ||
      c.expected_field_uT<0 || c.strong_field_uT<=0 || c.strong_field_uT>1000000 ||
      c.expected_field_uT>=c.strong_field_uT || c.field_tolerance_fraction<=0 ||
      c.field_tolerance_fraction>=1 || c.maximum_heading_innovation_rad<=0 ||
      c.maximum_heading_innovation_rad>kPi || std::fabs(c.reference_heading_rad)>kPi ||
      c.minimum_horizontal_fraction<=0 || c.minimum_horizontal_fraction>=1 ||
      c.maximum_imu_gap_us<1000 || c.maximum_imu_gap_us>1000000 ||
      c.maximum_magnetic_age_us<1000 || c.maximum_magnetic_age_us>1000000 ||
      c.recovery_good_us<10000 || c.recovery_good_us>60000000 ||
      c.recovery_ramp_us<10000 || c.recovery_ramp_us>60000000 || c.recovery_minimum_samples<2) return false;
  if (!finite(c.gyro_bias_dps) || !finite(c.hard_iron_uT)) return false;
  for (float value : c.gyro_bias_dps) if (std::fabs(value)>c.maximum_gyro_dps) return false;
  for (float value : c.hard_iron_uT) if (std::fabs(value)>1000000) return false;
  for (float value : c.soft_iron_matrix) if (!std::isfinite(value) || std::fabs(value)>10) return false;
  const auto &m=c.soft_iron_matrix;
  const double det=double(m[0])*(double(m[4])*m[8]-double(m[5])*m[7])-
      double(m[1])*(double(m[3])*m[8]-double(m[5])*m[6])+
      double(m[2])*(double(m[3])*m[7]-double(m[4])*m[6]);
  return det>1e-6; // No singular/reflecting calibration transform.
}
}

bool Filter::start(const Config &config, const Quaternion &initial) {
  auto q=initial;
  if (running_ || !valid_config(config) || !normalize_quaternion(q)) return false;
  *this=Filter{};
  config_=config;
  tilt_initialized_=!config.initialize_tilt_from_accel;
  running_=true;
  output_.quaternion=q;
  output_.mode=Mode::SixAxis;
  project(body_gravity(q));
  return true;
}
void Filter::stop() { *this=Filter{}; }
void Filter::clear_magnetic_trust(MagneticQuality quality, bool presence_known) {
  magnetic_trusted_=false;
  have_clean_streak_=false;
  clean_samples_=0;
  clean_streak_start_us_=last_clean_mag_us_=ramp_start_us_=0;
  output_.magnetic_weight=0;
  output_.magnetic_quality=quality;
  output_.magnetic_presence_valid=presence_known;
  output_.mode=running_ ? Mode::SixAxis : Mode::Stopped;
}
void Filter::suspect(MagneticQuality quality) {
  clear_magnetic_trust(quality,true);
  if (!output_.suspected_magnetic_proximity) {
    output_.suspected_magnetic_proximity=true;
    ++output_.proximity_event_sequence;
  }
}
void Filter::invalidate_sample() {
  output_.valid=false;
  output_.timestamp_us=0;
  output_.magnetic_sample_fresh=output_.magnetic_correction_applied=output_.accel_correction_applied=false;
  have_imu_time_=false;
  last_imu_us_=0;
  clear_magnetic_trust(MagneticQuality::Unavailable);
}
bool Filter::reset_orientation(const Quaternion &orientation) {
  auto q=orientation;
  if (!running_ || !normalize_quaternion(q)) return false;
  output_.quaternion=q;
  tilt_initialized_=true; // An explicit reset supplies a complete trusted pose.
  invalidate_sample();
  project(body_gravity(q));
  return true;
}
void Filter::project(const Vector3 &accel) {
  const auto &q=output_.quaternion;
  output_.euler_rad={{std::atan2(2*(q[0]*q[1]+q[2]*q[3]),1-2*(q[1]*q[1]+q[2]*q[2])),
    std::asin(std::clamp(2*(q[0]*q[2]-q[3]*q[1]),-1.0F,1.0F)),
    std::atan2(2*(q[0]*q[3]+q[1]*q[2]),1-2*(q[2]*q[2]+q[3]*q[3]))}};
  output_.gravity_g=body_gravity(q);
  for (unsigned axis=0;axis<3;++axis) output_.linear_accel_g[axis]=accel[axis]-output_.gravity_g[axis];
}

bool Filter::magnetic_update(const MagneticInput *mag, uint64_t now,
                             const Quaternion &predicted, Vector3 &rotation) {
  rotation={};
  output_.magnetic_age_us=have_mag_seen_ ? now-last_mag_us_ : 0;
  if (have_mag_seen_ && output_.magnetic_age_us>config_.maximum_magnetic_age_us)
    clear_magnetic_trust(MagneticQuality::Stale);
  if (!mag) return false; // Keep recent trust, NEVER reuse an old body vector.
  const bool time_valid=mag->timestamp_us>0 && mag->timestamp_us<=now &&
      now-mag->timestamp_us<=config_.maximum_magnetic_age_us;
  if (mag->overflow || mag->saturated) {
    const auto quality=mag->overflow ? MagneticQuality::Overflow : MagneticQuality::Saturated;
    if (time_valid) suspect(quality); else clear_magnetic_trust(quality);
    return false;
  }
  if (!mag->valid) { clear_magnetic_trust(MagneticQuality::Unavailable); return false; }
  if (!mag->timestamp_us) { clear_magnetic_trust(MagneticQuality::Invalid); return false; }
  if (mag->timestamp_us>now) { clear_magnetic_trust(MagneticQuality::Future); return false; }
  if (now-mag->timestamp_us>config_.maximum_magnetic_age_us) {
    clear_magnetic_trust(MagneticQuality::Stale); return false;
  }
  if (!finite(mag->field_uT)) { clear_magnetic_trust(MagneticQuality::Invalid); return false; }
  if (have_mag_seen_ && mag->timestamp_us<last_mag_us_) {
    clear_magnetic_trust(MagneticQuality::Invalid); return false;
  }
  if (have_mag_seen_ && (mag->timestamp_us==last_mag_us_ || mag->sequence==last_mag_sequence_)) {
    output_.magnetic_quality=MagneticQuality::Duplicate;
    return false; // No debounce, freshness renewal, weighting or correction.
  }
  last_mag_us_=mag->timestamp_us;
  last_mag_sequence_=mag->sequence;
  have_mag_seen_=true;
  output_.magnetic_sample_fresh=true;
  output_.magnetic_age_us=now-mag->timestamp_us;
  Vector3 field{};
  for (unsigned row=0;row<3;++row) {
    double value=0;
    for (unsigned column=0;column<3;++column)
      value+=double(config_.soft_iron_matrix[row*3+column])*
          (double(mag->field_uT[column])-config_.hard_iron_uT[column]);
    if (!std::isfinite(value) || std::fabs(value)>std::numeric_limits<float>::max()) {
      clear_magnetic_trust(MagneticQuality::Invalid); return false;
    }
    field[row]=float(value);
  }
  const double length=norm(field);
  output_.calibrated_field_uT=float(std::min(length,double(std::numeric_limits<float>::max())));
  if (length>=config_.strong_field_uT) { suspect(MagneticQuality::StrongField); return false; }
  if (config_.expected_field_uT==0) { clear_magnetic_trust(MagneticQuality::Uncalibrated); return false; }
  const double expected=config_.expected_field_uT,tolerance=config_.field_tolerance_fraction;
  if (!std::isfinite(length) || length<=0 || length<expected*(1-tolerance) ||
      length>expected*(1+tolerance)) {
    suspect(MagneticQuality::MagnitudeMismatch); return false;
  }
  const auto direction=world_vector(predicted,normalized(field,length));
  if (!finite(direction)) {clear_magnetic_trust(MagneticQuality::Invalid);return false;}
  const double horizontal=std::hypot(double(direction[0]),double(direction[1]));
  if (horizontal<config_.minimum_horizontal_fraction) {
    clear_magnetic_trust(MagneticQuality::InsufficientHorizontalField); return false;
  }
  const float innovation=std::remainder(std::atan2(direction[1],direction[0])-
      config_.reference_heading_rad,2*kPi);
  if (!std::isfinite(innovation)) {clear_magnetic_trust(MagneticQuality::Invalid);return false;}
  output_.heading_innovation_rad=innovation;
  if (std::fabs(innovation)>config_.maximum_heading_innovation_rad) {
    suspect(MagneticQuality::HeadingInnovation); return false;
  }
  const uint64_t previous_clean_us=last_clean_mag_us_;
  if (have_clean_streak_ && mag->timestamp_us-last_clean_mag_us_>config_.maximum_magnetic_age_us)
    clear_magnetic_trust(MagneticQuality::Recovering);
  if (!have_clean_streak_) {
    have_clean_streak_=true;
    clean_streak_start_us_=mag->timestamp_us;
    clean_samples_=1;
  } else if (clean_samples_<std::numeric_limits<uint16_t>::max()) ++clean_samples_;
  last_clean_mag_us_=mag->timestamp_us;
  if (!magnetic_trusted_) {
    if (mag->timestamp_us-clean_streak_start_us_<config_.recovery_good_us ||
        clean_samples_<config_.recovery_minimum_samples) {
      output_.magnetic_quality=MagneticQuality::Recovering;
      output_.magnetic_presence_valid=false;
      return false;
    }
    magnetic_trusted_=true;
    ramp_start_us_=mag->timestamp_us;
    if (output_.suspected_magnetic_proximity) {
      output_.suspected_magnetic_proximity=false;
      ++output_.proximity_event_sequence;
    }
  }
  output_.magnetic_presence_valid=true;
  output_.magnetic_weight=std::min(1.0F,float(double(mag->timestamp_us-ramp_start_us_)/config_.recovery_ramp_us));
  output_.magnetic_quality=output_.magnetic_weight<1 ? MagneticQuality::Recovering : MagneticQuality::FreshClean;
  output_.mode=output_.magnetic_weight>0 && config_.magnetic_gain>0 ? Mode::NineAxis : Mode::SixAxis;
  if (!previous_clean_us || output_.magnetic_weight<=0 || config_.magnetic_gain<=0) return false;
  // Fresh magnetic feedback uses elapsed MAGNETIC sample time, not IMU dt.
  // This preserves correction gain at slower magnetometer update rates without
  // applying the same body-frame observation on intervening IMU ticks.
  const double step_s=double(std::min(mag->timestamp_us-previous_clean_us,
      uint64_t(config_.maximum_magnetic_age_us)))*1e-6;
  const float angle=float(-std::sin(innovation)*config_.magnetic_gain*output_.magnetic_weight*step_s);
  const auto gravity=body_gravity(predicted);
  for (unsigned axis=0;axis<3;++axis) rotation[axis]=gravity[axis]*angle;
  return true;
}

UpdateCode Filter::update(const ImuInput &imu, const MagneticInput *mag) {
  if (!running_) return UpdateCode::Stopped;
  output_.valid=false;
  output_.magnetic_sample_fresh=output_.magnetic_correction_applied=output_.accel_correction_applied=false;
  if (!imu.valid || !imu.timestamp_us || !finite(imu.accel_g) || !finite(imu.gyro_dps)) {
    invalidate_sample(); return UpdateCode::InvalidImu;
  }
  for (unsigned axis=0;axis<3;++axis) {
    if (std::fabs(imu.accel_g[axis])>config_.maximum_input_accel_g ||
        std::fabs(imu.gyro_dps[axis])>config_.maximum_gyro_dps) {
      invalidate_sample(); return UpdateCode::InvalidImu;
    }
  }
  if (have_imu_time_ && imu.timestamp_us<=last_imu_us_) return UpdateCode::NonMonotonicImu;
  if (have_imu_time_ && imu.timestamp_us-last_imu_us_>config_.maximum_imu_gap_us) {
    last_imu_us_=imu.timestamp_us;
    clear_magnetic_trust(MagneticQuality::Stale);
    output_.timestamp_us=imu.timestamp_us;
    return UpdateCode::ImuGap;
  }
  const bool primed=!have_imu_time_;
  const float dt=primed ? 0 : float(double(imu.timestamp_us-last_imu_us_)*1e-6);
  auto q=output_.quaternion;
  const double accel_length=norm(imu.accel_g);
  if (dt>0) {
    Vector3 rotation{};
    for (unsigned axis=0;axis<3;++axis)
      rotation[axis]=(imu.gyro_dps[axis]-config_.gyro_bias_dps[axis])*kDegreesToRadians*dt;
    q=rotate_step(q,rotation);
  }
  // The acceleration sample describes this timestamp. Integrate gyro first so
  // a deferred bootstrap does not advance its newly established tilt twice.
  if (!tilt_initialized_ && accel_length>=config_.minimum_accel_g && accel_length<=config_.maximum_accel_g) {
    const auto measured=normalized(imu.accel_g,accel_length);
    const auto predicted=body_gravity(q);
    const double agreement=double(measured[0])*predicted[0]+double(measured[1])*predicted[1]+double(measured[2])*predicted[2];
    if (agreement<1-1e-6) {
      const float roll=std::atan2(measured[1],measured[2]);
      const float pitch=std::atan2(-measured[0],std::hypot(measured[1],measured[2]));
      const float heading=std::atan2(2*(q[0]*q[3]+q[1]*q[2]),1-2*(q[2]*q[2]+q[3]*q[3]));
      const float cr=std::cos(roll/2),sr=std::sin(roll/2),cp=std::cos(pitch/2),sp=std::sin(pitch/2);
      const float cy=std::cos(heading/2),sy=std::sin(heading/2);
      q={{cr*cp*cy+sr*sp*sy,sr*cp*cy-cr*sp*sy,cr*sp*cy+sr*cp*sy,cr*cp*sy-sr*sp*cy}};
      (void)normalize_quaternion(q);
    }
    tilt_initialized_=true;
  }
  if (dt>0) {
    Vector3 rotation{};
    if (config_.accel_gain>0 && accel_length>=config_.minimum_accel_g && accel_length<=config_.maximum_accel_g) {
      const auto error=cross(normalized(imu.accel_g,accel_length),body_gravity(q));
      for (unsigned axis=0;axis<3;++axis) rotation[axis]=config_.accel_gain*error[axis]*dt;
      q=rotate_step(q,rotation);
      output_.accel_correction_applied=true;
    }
  }
  Vector3 magnetic_rotation{};
  if (magnetic_update(mag,imu.timestamp_us,q,magnetic_rotation)) {
    q=rotate_step(q,magnetic_rotation);
    output_.magnetic_correction_applied=true;
  }
  if (!normalize_quaternion(q)) {invalidate_sample();return UpdateCode::NumericalFailure;}
  output_.quaternion=q;
  output_.timestamp_us=imu.timestamp_us;
  output_.valid=true;
  ++output_.sequence;
  project(imu.accel_g);
  last_imu_us_=imu.timestamp_us;
  have_imu_time_=true;
  return primed ? UpdateCode::Primed : UpdateCode::Updated;
}
} // namespace lunaredu::services::imu::fusion
