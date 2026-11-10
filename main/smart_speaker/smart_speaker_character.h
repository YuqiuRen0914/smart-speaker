#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace smart_speaker::character {

constexpr int kSize = 160;
// A 48-row tile needs 15 KiB of contiguous internal DMA memory, which may
// be unavailable once network services start. The surface then falls back
// to one SPI transaction per scanline. Keep each tile within 5 KiB instead.
constexpr int kTileRows = 16;
constexpr uint32_t kFrameMs = 33;
constexpr uint32_t kDrowsyMs = 60000;
constexpr uint32_t kSleepMs = 180000;

enum class Mood : uint8_t { Idle, Curious, Happy, Drowsy, Sleeping, Progress, Confused, Radar,
                          Listening, Excited, Surprised, Playful, Humming };
constexpr size_t kEyeShapes = 9;
enum class Action : uint8_t { None, Volume, Previous, Next, Play, Pause, Stop, Mode };
enum class Feedback : uint8_t { None, Hold, Connecting, Success, Error, Radar, Uploading };
enum class EdgeContact : uint8_t { None, Left, Right, Top, Bottom, Corner };

struct Input {
  bool playing = false;
  bool blocked = false;  // Settings, provisioning, upload or audio fault.
  uint32_t action_sequence = 0;
  Action action = Action::None;
  Feedback feedback = Feedback::None;
  uint8_t hold_percent = 0;
  int volume = 50;
  int max_volume = 70;
  float music_level = 0; // Pre-volume PCM envelope, not beat detection.
  bool motion_valid = false;
  float motion_tilt_x = 0;
  float motion_tilt_y = 0;
  float motion_twist_z = 0;
  float motion_angular_speed_dps = 0;
};

constexpr size_t kParticleLimit = 10;
struct ParticlePose {
  float x = 0, y = 0, radius = 0, angle = 0, opacity = 0;
  uint32_t rgb = 0;
  uint8_t shape = 0; // Ribbon, round, star.
};
struct Pose {
  std::array<ParticlePose, kParticleLimit> particles{};
  size_t particle_count = 0;
  Mood mood = Mood::Idle;
  bool visible = true;
  bool volume_visible = false;
  float volume_opacity = 0;
  float x = 0;
  float y = 0;
  float angle = 0;
  float scale_x = 1;
  float scale_y = 1;
  float gaze_x = 0;
  float gaze_y = 0;
  float lid = 1;
  float opacity = 1;
  std::array<float, kEyeShapes> eye_weights{1};
  float progress_morph = 0;
  float ring_start = 0;
  float ring_sweep = 0;
  float radar_phase = 0;
  EdgeContact edge_contact = EdgeContact::None;
  float edge_impact = 0;
};

// Fit the unchanged Pebble expression into the lyric layout's 160x136 viewport.
inline void fit_lyrics_layout(Pose &pose) {
  pose.scale_x *= .78f; pose.scale_y *= .78f;
  pose.x *= .78f; pose.y *= .78f;
  for (auto &p : pose.particles) {
    p.x = 80 + (p.x-80)*.78f;
    p.y = 78 + (p.y-78)*.78f;
    p.radius *= .78f;
  }
}

// Display-owned, allocation-free controller. Audio supplies only a snapshot.
class Controller {
 public:
  explicit Controller(uint32_t seed = 0x91e10da5U) : random_(seed ? seed : 1) {}
  Pose update(uint32_t now_ms, const Input &input);

 private:
  struct Spring { float value = 0; float velocity = 0; };
  static void advance(Spring &s, float target, float frequency, float dt);
  uint32_t random_between(uint32_t low, uint32_t high);
  void start_burst(uint32_t now);
  struct Particle {
    ParticlePose pose;
    float vx = 0, vy = 0, age = 0, life = 0, rotation_speed = 0;
  };
  std::array<Particle, kParticleLimit> particles_{};
  bool burst_active_ = false;
  uint32_t burst_ms_ = 0;
  bool initialized_ = false;
  bool was_playing_ = false;
  bool reacted_ = false;
  bool volume_seen_ = false;
  int previous_volume_ = 50;
  int volume_rise_ = 0;
  uint32_t volume_change_ms_ = 0;
  Mood volume_mood_ = Mood::Listening;
  uint32_t random_;
  uint32_t last_ms_ = 0;
  uint32_t activity_ms_ = 0;
  uint32_t entrance_ms_ = 0;
  uint32_t reaction_ms_ = 0;
  uint32_t volume_ms_ = 0;
  uint32_t action_sequence_ = 0;
  uint32_t gaze_ms_ = 0;
  uint32_t gaze_wait_ms_ = 3000;
  uint32_t blink_ms_ = 0;
  uint32_t blink_wait_ms_ = 4000;
  bool double_blink_ = false;
  Feedback feedback_ = Feedback::None;
  uint32_t feedback_ms_ = 0;
  uint32_t playback_elapsed_ms_ = 0;
  uint32_t playback_last_active_ms_ = 0;
  uint32_t nod_ms_ = 0, nod_wait_ms_ = 1800;
  Mood previous_mood_ = Mood::Idle;
  float phase_ = 0;
  float target_gaze_x_ = 0;
  float target_gaze_y_ = 0;
  Action reaction_ = Action::None;
  Spring x_, y_, angle_, squash_, gaze_x_, gaze_y_, lid_{1, 0};
  Spring edge_scale_x_{1, 0}, edge_scale_y_{1, 0};
  Spring progress_;
  Spring energy_;
  std::array<Spring, kEyeShapes> eye_weights_{{{1, 0}}};
  float filtered_tilt_x_ = 0;
  float filtered_tilt_y_ = 0;
  float edge_impact_ = 0;
  EdgeContact edge_contact_ = EdgeContact::None;
  bool edge_left_latched_ = false;
  bool edge_right_latched_ = false;
  bool edge_top_latched_ = false;
  bool edge_bottom_latched_ = false;
  uint32_t last_shake_ms_ = 0;
};

const char *mood_name(Mood mood);

// Geometry is prepared once per frame; each tile is cleared including margins.
// This avoids both per-pixel trig and trails when the body moves.
class Renderer {
 public:
  void prepare(const Pose &pose);
  bool render_tile(int top, int rows, uint16_t *pixels, size_t capacity) const;

 private:
  struct Point { float x = 0; float y = 0; };
  static constexpr size_t kBodyPoints = 64;
  static constexpr size_t kEyePoints = 32;
  static std::array<float, 2> polygon_row(const Point *points, size_t count, float y,
                          uint16_t color, uint16_t *pixels,
                          float clip_left = 0, float clip_right = kSize);
  std::array<float, 2> body_span(float y) const;
  std::array<Point, kBodyPoints> body_{};
  std::array<std::array<Point, kEyePoints>, 2> eyes_{};
  std::array<std::array<float, 2>, 2> eye_bounds_{};
  std::array<Point, kBodyPoints> progress_core_{};
  std::array<Point, kBodyPoints> progress_arc_{};
  float progress_morph_ = 0;
  bool has_arc_ = false;
  uint16_t arc_color_ = 0;
  uint16_t body_color_ = 0;
  uint16_t eye_color_ = 0;
  bool radar_ = false;
  struct RadarRing { float radius2 = 0; float inverse_diameter = 0; float half_width = 0; uint16_t ink = 0; };
  std::array<RadarRing, 3> radar_rings_{};
  struct PreparedParticle {
    std::array<Point, 10> points{};
    size_t count = 0;
    float top = 0, bottom = 0;
    uint16_t ink = 0;
    float opacity = 0;
  };
  std::array<PreparedParticle, kParticleLimit> particles_{};
  size_t particle_count_ = 0;
};

}  // namespace smart_speaker::character
