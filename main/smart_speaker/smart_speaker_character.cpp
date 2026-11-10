#include "smart_speaker_character.h"
#include "lunaredu_character_assets_pebble_geometry.h"

#include <algorithm>
#include <cmath>

namespace smart_speaker::character {
namespace pebble = lunaredu::character_assets::pebble;
namespace {
constexpr float kPi = 3.14159265358979323846f;
float smooth(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
uint16_t color(float opacity, int r, int g, int b) {
  opacity = std::clamp(opacity, 0.0f, 1.0f);
  return static_cast<uint16_t>((static_cast<int>(r * opacity) >> 3) << 11 |
                               (static_cast<int>(g * opacity) >> 2) << 5 |
                               (static_cast<int>(b * opacity) >> 3));
}
uint16_t blend(uint16_t bg, uint16_t fg, float coverage) {
  const int a = std::clamp(static_cast<int>(coverage * 256), 0, 256);
  const int r = (((bg >> 11) & 31) * (256 - a) + ((fg >> 11) & 31) * a) >> 8;
  const int g = (((bg >> 5) & 63) * (256 - a) + ((fg >> 5) & 63) * a) >> 8;
  const int b = ((bg & 31) * (256 - a) + (fg & 31) * a) >> 8;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}
constexpr float kPebbleRadius = 66.0f;
constexpr float kCanvasCenterX = 80.0f;
constexpr float kCanvasCenterY = 78.0f;
constexpr float kEdgePadding = 1.5f;
constexpr float kMotionAcceleration = 190.0f;
constexpr float kMotionMaxSpeed = 88.0f;
constexpr float kMotionDeadzone = 1.8f;
constexpr float kShakeThresholdDps = 165.0f;
constexpr uint32_t kShakeCooldownMs = 600;
constexpr float kCollisionRestitution = 0.42f;
constexpr float kCollisionVelocityScale = 1.0f / 100.0f;
constexpr float kCollisionImpactDecay = 4.8f;
constexpr float kEdgeContactTolerance = 0.75f;

float remove_deadzone(float value) {
  const float magnitude = std::abs(value);
  if (magnitude <= kMotionDeadzone) return 0;
  return std::copysign(magnitude - kMotionDeadzone, value);
}
}  // namespace

void Controller::advance(Spring &s, float target, float frequency, float dt) {
  // Bounded substeps keep blinks and wake-up stable after display/flash stalls.
  const int steps = std::max(1, static_cast<int>(std::ceil(dt / 0.008f)));
  const float step = dt / steps;
  for (int i = 0; i < steps; ++i) {
    s.velocity += (frequency * frequency * (target - s.value) -
                   1.8f * frequency * s.velocity) * step;
    s.value += s.velocity * step;
  }
}

uint32_t Controller::random_between(uint32_t low, uint32_t high) {
  random_ ^= random_ << 13;
  random_ ^= random_ >> 17;
  random_ ^= random_ << 5;
  return low + random_ % (high - low + 1);
}

void Controller::start_burst(uint32_t now) {
  burst_active_ = true;
  burst_ms_ = now;
  constexpr uint32_t palette[] = {0xf9705c, 0x5b95f0, 0x3fbe86, 0xf5b13f, 0x9a72ee, 0x35c3bd};
  // Motion Lab fx.js burst: radial velocity, upward bias, .94 drag,
  // gravity, .45-.85 second lifetime and its six-color/star palette.
  // Keep ten small particles inside the existing tile viewport; no extra DMA.
  for (size_t i = 0; i < particles_.size(); ++i) {
    auto &p = particles_[i];
    const float angle = i * (2 * kPi / kParticleLimit) + (int(random_between(0, 70)) - 35) * .01f;
    const float radius = random_between(96, 116) * (66.0f / 114.2705f);
    const float speed = random_between(170, 360) * .30f;
    p.pose.x = 80 + std::cos(angle) * radius;
    p.pose.y = 78 + std::sin(angle) * radius;
    p.vx = std::cos(angle) * speed;
    p.vy = std::sin(angle) * speed - random_between(20, 75) * .14f;
    p.age = 0; p.life = random_between(450, 850) * .001f;
    p.pose.radius = random_between(35, 70) * .05f;
    p.pose.angle = random_between(0, 360) * (kPi / 180);
    p.rotation_speed = (int(random_between(0, 520)) - 260) * (kPi / 180);
    const unsigned kind = random_between(0, 99);
    p.pose.shape = kind < 18 ? 2 : kind < 43 ? 1 : 0;
    p.pose.rgb = p.pose.shape == 2 ? 0xf4c34e : palette[random_between(0, 5)];
  }
}

Pose Controller::update(uint32_t now, const Input &input) {
  if (!initialized_) {
    initialized_ = true;
    last_ms_ = activity_ms_ = entrance_ms_ = gaze_ms_ = blink_ms_ = now;
    was_playing_ = input.playing;
    previous_volume_ = input.volume;
    action_sequence_ = input.action_sequence;
    playback_last_active_ms_ = now;
  }
  const uint32_t elapsed_ms = std::min<uint32_t>(now - last_ms_, 100);
  const float dt = elapsed_ms * 0.001f;
  last_ms_ = now;
  phase_ = std::fmod(phase_ + dt, 240.0f * kPi);
  if (input.feedback != feedback_) {
    feedback_ = input.feedback;
    feedback_ms_ = now;
  }
  const bool progress = feedback_ == Feedback::Hold || feedback_ == Feedback::Connecting || feedback_ == Feedback::Uploading;
  const bool success = feedback_ == Feedback::Success && now - feedback_ms_ < 1800;
  const bool failure = feedback_ == Feedback::Error;
  const bool radar = feedback_ == Feedback::Radar;
  const bool feedback_visible = progress || success || failure || radar;

  if (input.action_sequence != action_sequence_) {
    action_sequence_ = input.action_sequence;
    if (input.action == Action::Play || input.action == Action::Mode) start_burst(now);
    else burst_active_ = false;
    const bool repeated_limit = input.action == Action::Volume &&
        input.volume == previous_volume_ && (input.volume == 0 || input.volume == input.max_volume);
    if (!repeated_limit) {
      reaction_ = input.action;
      reaction_ms_ = now;
      reacted_ = true;
      if (reaction_ == Action::Volume) volume_mood_ = Mood::Listening;
    }
    activity_ms_ = now;
    if (input.action == Action::Volume) {
      volume_seen_ = true;
      volume_ms_ = now;
    }
  }
  if (input.volume != previous_volume_) {
    const int change = input.volume - previous_volume_;
    volume_rise_ = change > 0 ? (now - volume_change_ms_ < 450 ? volume_rise_ : 0) + change : 0;
    volume_change_ms_ = now;
    previous_volume_ = input.volume;
    volume_mood_ = input.volume == 0 || input.volume == input.max_volume ? Mood::Surprised :
                   volume_rise_ >= 10 ? Mood::Excited : Mood::Listening;
    reaction_ = Action::Volume;
    reacted_ = volume_seen_ = true;
    activity_ms_ = reaction_ms_ = volume_ms_ = now;
  }
  if (input.playing != was_playing_) {
    activity_ms_ = now;
    was_playing_ = input.playing;
  }
  if (input.playing || input.blocked || feedback_visible) activity_ms_ = now;

  const uint32_t idle_ms = now - activity_ms_;
  const uint32_t reaction_duration = reaction_ == Action::Volume ? 900 : 1100;
  const bool reacting = reacted_ && now - reaction_ms_ < reaction_duration && !input.blocked;
  // Quiet phrases alternate with the original Motion Lab reaction poses.
  // Pause for menus/feedback; decoder gaps preserve the phrase, a real stop
  // resets it. This is ambient choreography, not audio beat detection.
  if (input.playing) {
    playback_last_active_ms_ = now;
    if (!input.blocked && !feedback_visible && !reacting)
      playback_elapsed_ms_ = (playback_elapsed_ms_ + elapsed_ms) % 47000;
  } else if (now - playback_last_active_ms_ > 1500) playback_elapsed_ms_ = 0;
  const uint32_t phrase = playback_elapsed_ms_;
  const Mood playback_mood = phrase < 8000 ? Mood::Humming :
      phrase < 13000 ? Mood::Listening : phrase < 19000 ? Mood::Happy :
      phrase < 26000 ? Mood::Humming : phrase < 31000 ? Mood::Playful :
      phrase < 35000 ? Mood::Curious : phrase < 38000 ? Mood::Humming :
      phrase < 40500 ? Mood::Excited : Mood::Humming;
  Pose pose;
  if (input.blocked || feedback_visible || now - burst_ms_ >= 900) burst_active_ = false;
  if (burst_active_) {
    const float drag = std::pow(.94f, dt * 60);
    for (auto &p : particles_) {
      p.age += dt;
      if (p.age >= p.life) continue;
      p.pose.x += p.vx * dt; p.pose.y += p.vy * dt;
      p.vx *= drag; p.vy = p.vy * drag + 40 * .14f * dt;
      p.pose.angle += p.rotation_speed * dt;
      const float age = p.age / p.life;
      auto visible = p.pose;
      visible.opacity = age < .1f ? age * 10 : std::pow(1 - (age - .1f) / .9f, 1.7f);
      visible.radius *= 1 - age * .4f;
      pose.particles[pose.particle_count++] = visible;
    }
  }
  pose.visible = !input.blocked;
  pose.volume_visible = !feedback_visible && volume_seen_ && now - volume_ms_ < 1500;
  pose.volume_opacity = pose.volume_visible ? std::clamp((1500.0f - (now - volume_ms_)) / 250.0f, 0.0f, 1.0f) : 0;
  pose.mood = idle_ms >= kSleepMs ? Mood::Sleeping :
              idle_ms >= kDrowsyMs ? Mood::Drowsy : input.playing ? playback_mood : Mood::Idle;
  if (reacting) {
    pose.mood = reaction_ == Action::Volume ? volume_mood_ :
                reaction_ == Action::Previous || reaction_ == Action::Next ? Mood::Playful :
                reaction_ == Action::Pause ? Mood::Idle : Mood::Happy;
  }
  if (radar) pose.mood = Mood::Radar;
  else if (progress) pose.mood = Mood::Progress;
  else if (success) pose.mood = Mood::Happy;
  else if (failure) pose.mood = Mood::Confused;
  const bool sleeping = pose.mood == Mood::Sleeping;
  const bool drowsy = pose.mood == Mood::Drowsy;
  if (now - gaze_ms_ >= gaze_wait_ms_) {
    gaze_ms_ = now;
    gaze_wait_ms_ = pose.mood == Mood::Curious || pose.mood == Mood::Playful ?
        random_between(1100, 2200) : random_between(3000, 6500);
    target_gaze_x_ = static_cast<int>(random_between(0, 120)) * 0.1f - 6;
    target_gaze_y_ = static_cast<int>(random_between(0, 50)) * 0.1f - 2;
  }
  float look_x = sleeping ? 0 : target_gaze_x_;
  float look_y = drowsy || sleeping ? 3 : target_gaze_y_;
  if (reacting) {
    look_x = reaction_ == Action::Previous ? -10 :
             reaction_ == Action::Next ? 10 : 0;
    look_y = reaction_ == Action::Volume ? 6 : -1;
  }
  if (now - blink_ms_ >= blink_wait_ms_ + 420) {
    blink_ms_ = now;
    blink_wait_ms_ = random_between(4500, 10000);
    double_blink_ = random_between(0, 3) == 0;
  }
  const uint32_t blink_elapsed = now - blink_ms_;
  const bool blink = (blink_elapsed >= blink_wait_ms_ && blink_elapsed < blink_wait_ms_ + 95) ||
      (double_blink_ && blink_elapsed >= blink_wait_ms_ + 220 && blink_elapsed < blink_wait_ms_ + 305);
  // Motion Lab emphasis uses eyeScale >= 1.32 and open lids >= 1.18.
  // Preserve the speaker's natural blink/sleep transitions.
  float target_lid = drowsy ? 0.45f : sleeping ? 1 : 1.18f;
  if (blink && !sleeping) target_lid = 0.06f;

  const float breath = std::sin(phase_ * (sleeping ? 0.5f : 0.8f));
  const float response = reacting ? std::sin(smooth((now - reaction_ms_) / 1400.0f) * kPi) : 0;
  float tx = std::sin(phase_ * 0.3f) * 0.7f;
  float ty = breath * (sleeping ? 1.6f : 1.0f) + (sleeping ? 5 : 0) - response * 3;
  float angle = std::sin(phase_ * 0.4f) * 0.025f + look_x * 0.004f + (sleeping ? 0.05f : 0);
  float squash = breath * 0.009f + response * 0.022f;
  // Motion Lab src/pose.js applyPose: original state frequencies and curves,
  // scaled from its 114.2705 radius to our 66-pixel Pebble. Input selects a
  // state and gaze direction; it does not enqueue separate animations.
  constexpr float px = 66.0f / 114.2705f, degrees = kPi / 180;
  if (pose.mood == Mood::Humming) {
    angle = std::sin(phase_ * 0.4f) * 2 * degrees;
    tx = std::sin(phase_ * 0.3f) * 1.5f * px;
    ty = std::sin(phase_ * 0.7f) * 1.5f * px;
    squash = 0;
  } else if (pose.mood == Mood::Listening) {
    if (previous_mood_ != Mood::Listening || now - nod_ms_ >= nod_wait_ms_) {
      nod_ms_ = now;
      nod_wait_ms_ = random_between(1800, 3200);
    }
    const float nod = std::sin(std::min((now - nod_ms_) / 380.0f, 1.0f) * kPi);
    angle = (8 + std::sin(phase_ * 0.5f) * 1.5f + nod * 2) * degrees;
    tx = 2 * px;
    ty = (-2 + std::sin(phase_ * 0.8f) * 0.8f + nod * 4.5f) * px;
    squash = 0.015f;
  } else if (pose.mood == Mood::Curious) {
    if (previous_mood_ != Mood::Curious || now - nod_ms_ >= nod_wait_ms_) {
      nod_ms_ = now; nod_wait_ms_ = random_between(1600, 2800);
    }
    const float nod = std::sin(std::min((now - nod_ms_) / 440.0f, 1.0f) * kPi);
    angle = (10 + std::sin(phase_ * .7f) * 6 + nod * 5) * degrees;
    tx = (std::sin(phase_ * .6f) * 5 + nod * 8) * px;
    ty = (-2 + std::sin(phase_ * .9f) * 1.5f) * px;
    squash = .01f;
  } else if (pose.mood == Mood::Happy) {
    const float wave = std::sin(phase_ * 2.4f);
    angle = std::sin(phase_ * 1.2f) * 3 * degrees;
    tx = std::sin(phase_ * 1.1f) * 2.5f * px;
    ty = -std::abs(wave) * 3 * px;
    squash = wave * 0.02f;
  } else if (pose.mood == Mood::Excited) {
    const float beat = std::fmod(phase_ * 2.2f, 1.0f);
    ty = (-std::sin(beat * kPi) * 10 + 2) * px;
    tx = std::sin(phase_ * 1.1f) * 4 * px;
    angle = std::sin(phase_ * 2 * kPi * 1.1f) * 7 * degrees;
    squash = beat < 0.1f ? -0.08f : beat < 0.3f ? 0.05f : 0;
  } else if (pose.mood == Mood::Surprised) {
    const float elapsed = (now - reaction_ms_) * 0.001f;
    const float fade = 1 - std::min(elapsed / 1.2f, 1.0f);
    tx = -4 * fade * px;
    ty = -8 * fade * px;
    angle = std::sin(phase_ * 11) * 1.5f * fade * degrees;
    squash = elapsed < 0.2f ? 0.08f : 0;
  } else if (pose.mood == Mood::Playful) {
    angle = (std::sin(phase_ * 1.4f) * 8 + look_x * 0.3f) * degrees;
    tx = std::sin(phase_ * 1.1f) * 4 * px;
    ty = -std::abs(std::sin(phase_ * 2.2f)) * 3 * px;
    squash = std::sin(phase_ * 2.2f) * 0.015f;
  }
  previous_mood_ = pose.mood;
  const bool music_motion = input.playing && !input.blocked && !feedback_visible && !reacting;
  const float level = std::isfinite(input.music_level) ? std::clamp(input.music_level, 0.0f, 1.0f) : 0;
  advance(energy_, music_motion ? level : 0, 12, dt);
  if (music_motion) {
    const float energy = std::clamp(energy_.value, 0.0f, 1.0f);
    // Small audio-driven accents layer onto the source poses and ease away
    // during controls. Keep the eyes legible and avoid sustained confetti.
    ty -= energy * 2.5f;
    squash += energy * .018f;
    angle += std::sin(phase_ * 1.6f) * energy * .025f;
  }
  const bool motion_active = input.motion_valid && !input.blocked && !feedback_visible;
  if (motion_active) {
    const float filter = std::clamp(dt * 12.0f, 0.08f, 0.65f);
    filtered_tilt_x_ += (std::clamp(input.motion_tilt_x, -25.0f, 25.0f) -
                         filtered_tilt_x_) * filter;
    filtered_tilt_y_ += (std::clamp(input.motion_tilt_y, -25.0f, 25.0f) -
                         filtered_tilt_y_) * filter;
    const float tilt_x = remove_deadzone(filtered_tilt_x_);
    const float tilt_y = remove_deadzone(filtered_tilt_y_);
    // Tilt is an acceleration, not a position assignment. The existing
    // character springs still provide the return force to the mood pose.
    // Keep the physical sensor axes aligned with the screen axes:
    // MPU X (left/right tilt) drives screen X, MPU Y (front/back tilt)
    // drives screen Y. Do not swap these when changing the visual pose.
    x_.velocity = std::clamp(x_.velocity + tilt_x * kMotionAcceleration * dt,
                             -kMotionMaxSpeed, kMotionMaxSpeed);
    y_.velocity = std::clamp(y_.velocity + tilt_y * kMotionAcceleration * dt,
                             -kMotionMaxSpeed, kMotionMaxSpeed);
    const float twist_z = std::clamp(input.motion_twist_z, -30.0f, 30.0f);
    const float motion_amount = std::clamp(
        std::max({std::abs(tilt_x), std::abs(tilt_y), std::abs(twist_z)}) /
            25.0f,
        0.0f, 1.0f);
    angle += twist_z * 0.0065f + tilt_x * 0.0035f;
    squash += motion_amount * 0.012f;
    look_x += tilt_x * 0.38f;
    look_y += tilt_y * 0.25f;
    if (input.motion_angular_speed_dps >= kShakeThresholdDps &&
        now - last_shake_ms_ >= kShakeCooldownMs) {
      const float strength = std::clamp(
          (input.motion_angular_speed_dps - kShakeThresholdDps) / 180.0f,
          0.0f, 1.0f);
      y_.velocity = std::clamp(y_.velocity - (34.0f + strength * 42.0f),
                               -kMotionMaxSpeed, kMotionMaxSpeed);
      x_.velocity = std::clamp(x_.velocity +
                                   std::copysign(18.0f + strength * 22.0f,
                                       filtered_tilt_x_ == 0 ? 1.0f : filtered_tilt_x_),
                               -kMotionMaxSpeed, kMotionMaxSpeed);
      last_shake_ms_ = now;
    }
  } else {
    filtered_tilt_x_ *= std::exp(-dt * 8.0f);
    filtered_tilt_y_ *= std::exp(-dt * 8.0f);
    edge_left_latched_ = edge_right_latched_ = false;
    edge_top_latched_ = edge_bottom_latched_ = false;
  }
  advance(x_, tx, 7, dt);
  advance(y_, ty, 10, dt);
  advance(angle_, angle, 9, dt);
  advance(squash_, squash, 14, dt);
  if (motion_active) {
    const float min_x = kEdgePadding + kPebbleRadius - kCanvasCenterX;
    const float max_x = kSize - kEdgePadding - kPebbleRadius - kCanvasCenterX;
    const float min_y = kEdgePadding + kPebbleRadius - kCanvasCenterY;
    const float max_y = kSize - kEdgePadding - kPebbleRadius - kCanvasCenterY;
    bool touching_x = false, touching_y = false;
    EdgeContact contact = EdgeContact::None;
    if (x_.value <= min_x + kEdgeContactTolerance) {
      touching_x = true;
      x_.value = min_x;
      if (!edge_left_latched_) {
        edge_impact_ = std::max(edge_impact_, std::clamp(
            std::abs(x_.velocity) * kCollisionVelocityScale, 0.18f, 1.0f));
        if (x_.velocity < 0) x_.velocity = -x_.velocity * kCollisionRestitution;
      }
      edge_left_latched_ = true;
      contact = EdgeContact::Left;
    } else if (x_.value > min_x + 3.0f) {
      edge_left_latched_ = false;
    }
    if (x_.value >= max_x - kEdgeContactTolerance) {
      touching_x = true;
      x_.value = max_x;
      if (!edge_right_latched_) {
        edge_impact_ = std::max(edge_impact_, std::clamp(
            std::abs(x_.velocity) * kCollisionVelocityScale, 0.18f, 1.0f));
        if (x_.velocity > 0) x_.velocity = -x_.velocity * kCollisionRestitution;
      }
      edge_right_latched_ = true;
      contact = EdgeContact::Right;
    } else if (x_.value < max_x - 3.0f) {
      edge_right_latched_ = false;
    }
    if (y_.value <= min_y + kEdgeContactTolerance) {
      touching_y = true;
      y_.value = min_y;
      if (!edge_top_latched_) {
        edge_impact_ = std::max(edge_impact_, std::clamp(
            std::abs(y_.velocity) * kCollisionVelocityScale, 0.18f, 1.0f));
        if (y_.velocity < 0) y_.velocity = -y_.velocity * kCollisionRestitution;
      }
      edge_top_latched_ = true;
      contact = touching_x ? EdgeContact::Corner : EdgeContact::Top;
    } else if (y_.value > min_y + 3.0f) {
      edge_top_latched_ = false;
    }
    if (y_.value >= max_y - kEdgeContactTolerance) {
      touching_y = true;
      y_.value = max_y;
      if (!edge_bottom_latched_) {
        edge_impact_ = std::max(edge_impact_, std::clamp(
            std::abs(y_.velocity) * kCollisionVelocityScale, 0.18f, 1.0f));
        if (y_.velocity > 0) y_.velocity = -y_.velocity * kCollisionRestitution;
      }
      edge_bottom_latched_ = true;
      contact = touching_x ? EdgeContact::Corner : EdgeContact::Bottom;
    } else if (y_.value < max_y - 3.0f) {
      edge_bottom_latched_ = false;
    }
    if (touching_x || touching_y) edge_contact_ = contact;
  }
  edge_impact_ *= std::exp(-dt * kCollisionImpactDecay);
  if (edge_impact_ < 0.015f) {
    edge_impact_ = 0;
    edge_contact_ = EdgeContact::None;
  }
  float edge_target_x = 1, edge_target_y = 1;
  const float impact = std::clamp(edge_impact_, 0.0f, 1.0f);
  switch (edge_contact_) {
    case EdgeContact::Left:
    case EdgeContact::Right:
      edge_target_x = 1 - impact * 0.28f;
      edge_target_y = 1 + impact * 0.18f;
      break;
    case EdgeContact::Top:
    case EdgeContact::Bottom:
      edge_target_x = 1 + impact * 0.18f;
      edge_target_y = 1 - impact * 0.28f;
      break;
    case EdgeContact::Corner:
      edge_target_x = edge_target_y = 1 - impact * 0.22f;
      break;
    case EdgeContact::None:
      break;
  }
  advance(edge_scale_x_, edge_target_x, 20, dt);
  advance(edge_scale_y_, edge_target_y, 20, dt);
  advance(gaze_x_, look_x, 14, dt);
  advance(gaze_y_, look_y, 14, dt);
  advance(lid_, target_lid, 38, dt);
  pose.x = x_.value;
  pose.y = y_.value;
  pose.angle = angle_.value;
  pose.scale_x = (1 - squash_.value * 0.6f) * edge_scale_x_.value;
  pose.scale_y = (1 + squash_.value) * edge_scale_y_.value;
  pose.gaze_x = gaze_x_.value;
  pose.gaze_y = gaze_y_.value;
  pose.lid = std::clamp(lid_.value, 0.06f, 1.22f);
  pose.opacity = smooth((now - entrance_ms_) / 400.0f);
  if (feedback_visible) pose.opacity = 1;
  const size_t eye_index = pose.mood == Mood::Curious ? 1 : pose.mood == Mood::Happy ? 2 :
                           sleeping ? 3 : failure ? 4 : pose.mood == Mood::Listening ? 5 :
                           pose.mood == Mood::Excited ? 6 : pose.mood == Mood::Surprised ? 7 :
                           pose.mood == Mood::Playful ? 8 : 0;
  float weight_sum = 0;
  for (size_t i = 0; i < eye_weights_.size(); ++i) {
    advance(eye_weights_[i], i == eye_index ? 1 : 0, 10, dt);
    pose.eye_weights[i] = std::clamp(eye_weights_[i].value, 0.0f, 1.0f);
    weight_sum += pose.eye_weights[i];
  }
  for (float &weight : pose.eye_weights) weight /= std::max(weight_sum, 0.001f);
  advance(progress_, progress && !input.blocked ? 1 : 0, 13, dt);
  pose.progress_morph = std::clamp(progress_.value, 0.0f, 1.0f);
  pose.radar_phase = (now % 1300) / 1300.0f;
  pose.edge_contact = edge_contact_;
  pose.edge_impact = edge_impact_;
  if (feedback_ == Feedback::Hold || feedback_ == Feedback::Uploading) {
    pose.ring_start = -kPi / 2;
    pose.ring_sweep = std::min<int>(input.hold_percent, 100) * (2 * kPi / 100);
  } else {
    // Unknown network duration: a moving arc, never a fabricated percentage.
    const float elapsed = (now - feedback_ms_) * 0.001f;
    pose.ring_start = elapsed * 2 - kPi / 2;
    pose.ring_sweep = 1.8f + 0.8f * std::sin(elapsed * 1.7f);
  }
  return pose;
}

const char *mood_name(Mood mood) {
  switch (mood) {
    case Mood::Idle: return "idle";
    case Mood::Curious: return "curious";
    case Mood::Happy: return "happy";
    case Mood::Drowsy: return "drowsy";
    case Mood::Sleeping: return "sleeping";
    case Mood::Progress: return "progress";
    case Mood::Confused: return "confused";
    case Mood::Radar: return "radar";
    case Mood::Listening: return "listening";
    case Mood::Excited: return "excited";
    case Mood::Surprised: return "surprised";
    case Mood::Playful: return "playful";
    case Mood::Humming: return "humming";
  }
  return "idle";
}

void Renderer::prepare(const Pose &pose) {
  particle_count_ = std::min(pose.particle_count, kParticleLimit);
  for (size_t i = 0; i < particle_count_; ++i) {
    const auto &p = pose.particles[i];
    auto &out = particles_[i];
    out.top = kSize; out.bottom = 0;
    out.count = p.shape == 2 ? 10 : p.shape == 1 ? 8 : 4;
    const float c = std::cos(p.angle), s = std::sin(p.angle);
    for (size_t v = 0; v < out.count; ++v) {
      const float theta = -kPi / 2 + v * 2 * kPi / out.count;
      const float radius = p.shape == 2 && v % 2 ? p.radius * .42f : p.radius;
      const float x = std::cos(theta) * radius * (p.shape == 0 ? 1.5f : 1);
      const float y = std::sin(theta) * radius;
      out.points[v] = {p.x + x * c - y * s, p.y + x * s + y * c};
      out.top = std::min(out.top, out.points[v].y);
      out.bottom = std::max(out.bottom, out.points[v].y);
    }
    out.ink = color(p.opacity * pose.opacity, (p.rgb >> 16) & 255, (p.rgb >> 8) & 255, p.rgb & 255);
  }
  radar_ = pose.mood == Mood::Radar;
  if (radar_) {
    // Character Motion Lab src/fx.js paintRadar + RADIUS.radar:
    // central radius 19; three expanding rings to 104, staggered by 1/3,
    // 1300 ms period, stroke 3.4*(1-phase*.55), opacity (1-phase)*.9.
    constexpr float scale = 44.0f / 104;
    for (size_t i = 0; i < radar_rings_.size(); ++i) {
      const float phase = std::fmod(pose.radar_phase + i / 3.0f, 1.0f);
      const float radius = (19 + (104 - 19) * phase) * scale;
      radar_rings_[i] = {radius * radius, 1 / (2 * radius),
          3.4f * (1 - phase * 0.55f) * scale / 2,
          color(pose.opacity * (1 - phase) * 0.9f, 232, 230, 223)};
    }
    arc_color_ = color(pose.opacity, 232, 230, 223);
    return;
  }
  const float morph = std::clamp(pose.progress_morph, 0.0f, 1.0f);
  progress_morph_ = morph;
  const float body_weight = 1 - morph;
  const float c = std::cos(pose.angle * body_weight), s = std::sin(pose.angle * body_weight);
  auto transform = [&](float x, float y) -> Point {
    x *= 1 + (pose.scale_x - 1) * body_weight;
    y *= 1 + (pose.scale_y - 1) * body_weight;
    return {80 + pose.x * body_weight + x * c - y * s,
            78 + pose.y * body_weight + x * s + y * c};
  };
  // Baked reference contour and eye topology; no live SVG parser or 3D engine.
  for (size_t i = 0; i < body_.size(); ++i) {
    const auto base = pebble::kBody[i];
    if (morph <= 0.001f) {
      body_[i] = transform(66 * base.x, 66 * base.y);
      continue; // No ring trigonometry during ordinary character frames.
    }
    const float a = std::atan2(base.y, base.x);
    body_[i] = transform(66 * base.x * body_weight + 44 * std::cos(a) * morph,
                         66 * base.y * body_weight + 44 * std::sin(a) * morph);
    const float hole_angle = 2 * kPi * i / body_.size();
    const float hole_radius = 41.5f * smooth((morph - 0.15f) / 0.85f);
    progress_core_[i] = transform(hole_radius * std::cos(hole_angle), hole_radius * std::sin(hole_angle));
    const size_t n = i < 32 ? i : 63 - i;
    const float arc_angle = pose.ring_start + std::clamp(pose.ring_sweep, 0.0f, 2*kPi) * n / 31;
    const float arc_radius = i < 32 ? 44 : 41.5f;
    progress_arc_[i] = transform(arc_radius * std::cos(arc_angle), arc_radius * std::sin(arc_angle));
  }
  const pebble::Point *shapes[kEyeShapes][2] = {
      {pebble::kIdle0, pebble::kIdle1}, {pebble::kCurious0, pebble::kCurious1},
      {pebble::kHappy0, pebble::kHappy1}, {pebble::kSleeping0, pebble::kSleeping1},
      {pebble::kConfused0, pebble::kConfused1}, {pebble::kListening0, pebble::kListening1},
      {pebble::kExcited0, pebble::kExcited1}, {pebble::kSurprised0, pebble::kSurprised1},
      {pebble::kPlayful0, pebble::kPlayful1}};
  std::array<std::array<Point, kEyePoints>, 2> mixed{};
  std::array<Point, 2> centers{};
  for (size_t eye = 0; morph < 0.3f && eye < eyes_.size(); ++eye) {
    for (size_t i = 0; i < kEyePoints; ++i) {
      for (size_t mood = 0; mood < kEyeShapes; ++mood) {
        mixed[eye][i].x += shapes[mood][eye][i].x * pose.eye_weights[mood] * 66;
        mixed[eye][i].y += shapes[mood][eye][i].y * pose.eye_weights[mood] * 66;
      }
      centers[eye].x += mixed[eye][i].x / kEyePoints;
      centers[eye].y += mixed[eye][i].y / kEyePoints;
    }
  }
  const Point pull{-(centers[0].x + centers[1].x) * .21f,
                   -(centers[0].y + centers[1].y) * .21f};
  const size_t left = centers[0].x < centers[1].x ? 0 : 1, right = 1 - left;
  float inner_left = 0, inner_right = 0;
  for (size_t i = 0; i < kEyePoints; ++i) {
    inner_left = std::max(inner_left, mixed[left][i].x - centers[left].x);
    inner_right = std::max(inner_right, centers[right].x - mixed[right][i].x);
  }
  const float eye_scale = std::min(1.32f, std::max(.35f,
      (centers[right].x - centers[left].x - 3) / std::max(.01f, inner_left + inner_right)));
  for (size_t eye = 0; morph < 0.3f && eye < eyes_.size(); ++eye) {
    const float cx = centers[eye].x, cy = centers[eye].y;
    for (size_t i = 0; i < kEyePoints; ++i)
      eyes_[eye][i] = transform((cx + (mixed[eye][i].x - cx) * eye_scale + pull.x + pose.gaze_x) * body_weight,
                                (cy + (mixed[eye][i].y - cy) * eye_scale * pose.lid + pull.y + pose.gaze_y) * body_weight);
    // Follow the reference's live silhouette constraint when gaze moves.
    float left_shift = -kSize, right_shift = kSize;
    for (const auto &point : eyes_[eye]) {
      const auto span = body_span(point.y);
      left_shift = std::max(left_shift, span[0] + 1.5f - point.x);
      right_shift = std::min(right_shift, span[1] - 1.5f - point.x);
    }
    const float shift = left_shift <= right_shift ?
        std::clamp(0.0f, left_shift, right_shift) : (left_shift + right_shift) / 2;
    eye_bounds_[eye] = {static_cast<float>(kSize), 0};
    for (auto &point : eyes_[eye]) {
      point.x += shift;
      eye_bounds_[eye][0] = std::min(eye_bounds_[eye][0], point.y);
      eye_bounds_[eye][1] = std::max(eye_bounds_[eye][1], point.y);
    }
  }
  arc_color_ = color(pose.opacity, 232, 230, 223);
  // Match the supplied Motion Lab screenshot: #E3B44F / #F1EFE7.
  body_color_ = blend(color(pose.opacity, 227, 180, 79), color(pose.opacity, 46, 45, 43), morph);
  eye_color_ = blend(color(pose.opacity, 241, 239, 231), body_color_, smooth(morph / 0.3f));
  has_arc_ = morph > 0.5f && pose.ring_sweep > 0.001f;
}

std::array<float, 2> Renderer::body_span(float y) const {
  float left = kSize, right = 0;
  for (size_t i = 0; i < body_.size(); ++i) {
    const auto &a = body_[i], &b = body_[(i + 1) % body_.size()];
    if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) {
      const float x = a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y);
      left = std::min(left, x);
      right = std::max(right, x);
    }
  }
  return left <= right ? std::array<float, 2>{left, right} : std::array<float, 2>{80, 80};
}

std::array<float, 2> Renderer::polygon_row(const Point *points, size_t count, float y,
                            uint16_t ink, uint16_t *pixels, float clip_left, float clip_right) {
  // Sort intersections to also support the concave smiling eye contours.
  std::array<float, kBodyPoints> intersections{};
  size_t hits = 0;
  for (size_t i = 0; i < count; ++i) {
    const Point &a = points[i], &b = points[(i + 1) % count];
    if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) {
      intersections[hits++] = a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y);
    }
  }
  std::sort(intersections.begin(), intersections.begin() + hits);
  for (size_t i = 0; i + 1 < hits; i += 2) {
    const float left = std::clamp(intersections[i], clip_left, clip_right);
    const float right = std::clamp(intersections[i + 1], clip_left, clip_right);
    if (right <= left) continue;
    const int first = static_cast<int>(left);
    const int last = static_cast<int>(right);
    if (first == last) {
      pixels[first] = blend(pixels[first], ink, right - left);
      continue;
    }
    // Only the two boundary pixels need coverage blending. The interior is
    // opaque; filling it avoids thousands of floating-point blends per frame.
    pixels[first] = blend(pixels[first], ink, first + 1.0f - left);
    std::fill(pixels + first + 1, pixels + last, ink);
    if (right > last) pixels[last] = blend(pixels[last], ink, right - last);
  }
  return hits >= 2 ? std::array<float, 2>{intersections[0], intersections[hits - 1]} :
                     std::array<float, 2>{80, 80};
}

bool Renderer::render_tile(int top, int rows, uint16_t *pixels, size_t capacity) const {
  if (!pixels || top < 0 || rows <= 0 || rows > kTileRows ||
      top > kSize - rows || capacity < static_cast<size_t>(kSize * rows)) return false;
  std::fill_n(pixels, kSize * rows, uint16_t{0});
  for (int row = 0; row < rows; ++row) {
    uint16_t *out = pixels + row * kSize;
    if (radar_) {
      // Source radial waves, no sweep line or grid. Squared-distance edge
      // coverage avoids per-pixel sqrt/trig while preserving subpixel strokes.
      const float y = top + row + 0.5f - 80;
      for (int x = 36; x < 124; ++x) {
        const float dx = x + 0.5f - 80;
        const float radius2 = dx * dx + y * y;
        uint16_t ink = 0;
        for (const auto &ring : radar_rings_) {
          const float edge = std::abs(radius2 - ring.radius2) * ring.inverse_diameter;
          const float coverage = std::clamp(ring.half_width + 0.5f - edge, 0.0f, 1.0f);
          if (coverage > 0) ink = blend(ink, ring.ink, coverage);
        }
        constexpr float body_radius = 19 * 44.0f / 104;
        const float body_coverage = std::clamp(0.5f +
            (body_radius * body_radius - radius2) / (2 * body_radius), 0.0f, 1.0f);
        if (body_coverage > 0) ink = blend(ink, arc_color_, body_coverage);
        out[x] = ink;
      }
      continue;
    }
    const float scan_y = top + row + .5f;
    for (size_t i = 0; i < particle_count_; ++i) {
      const auto &p = particles_[i];
      if (scan_y >= p.top && scan_y < p.bottom)
        polygon_row(p.points.data(), p.count, scan_y, p.ink, out);
    }
    const auto span = polygon_row(body_.data(), body_.size(), scan_y, body_color_, out);
    if (progress_morph_ < 0.3f) {
      for (size_t i = 0; i < eyes_.size(); ++i) {
        if (scan_y < eye_bounds_[i][0] || scan_y >= eye_bounds_[i][1]) continue;
        const auto &eye = eyes_[i];
        polygon_row(eye.data(), eye.size(), scan_y, eye_color_, out,
                    std::clamp(span[0], 0.0f, static_cast<float>(kSize)),
                    std::clamp(span[1], 0.0f, static_cast<float>(kSize)));
      }
    }
    if (progress_morph_ > 0.15f)
      polygon_row(progress_core_.data(), progress_core_.size(), top + row + 0.5f, 0, out);
    if (has_arc_)
      polygon_row(progress_arc_.data(), progress_arc_.size(), top + row + 0.5f,
                  blend(body_color_, arc_color_, smooth((progress_morph_ - 0.5f) * 2)), out);
  }
  return true;
}

}  // namespace smart_speaker::character
