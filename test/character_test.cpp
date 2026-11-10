#include "smart_speaker_character.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

using namespace smart_speaker::character;

static void state_tests(uint32_t start) {
  Controller controller;
  Input input;
  assert(controller.update(start, input).visible);
  assert(controller.update(start + 500, input).opacity == 1);
  assert(controller.update(start + kDrowsyMs, input).mood == Mood::Drowsy);
  assert(controller.update(start + kSleepMs, input).mood == Mood::Sleeping);
  input.action = Action::Volume;
  ++input.action_sequence;
  auto pose = controller.update(start + kSleepMs + 20, input);
  assert(pose.mood == Mood::Listening && pose.volume_visible);
  assert(controller.update(start + kSleepMs + 2000, input).mood == Mood::Idle);
  assert(!controller.update(start + kSleepMs + 2000, input).volume_visible);

  input.playing = true;
  pose = controller.update(start + kSleepMs + 3000, input);
  assert(pose.visible);
  assert(controller.update(start + kSleepMs + 3400, input).visible);
  assert(controller.update(start + kSleepMs + 3400, input).opacity == 1);
  // Playback and short decoder gaps keep the same visible character.
  input.playing = false;
  assert(controller.update(start + kSleepMs + 4000, input).visible);
  assert(controller.update(start + kSleepMs + 4500, input).visible);
  input.playing = true;
  assert(controller.update(start + kSleepMs + 4600, input).visible);
  input.playing = false;
  controller.update(start + kSleepMs + 5000, input);
  assert(controller.update(start + kSleepMs + 5900, input).visible);
  input.blocked = true;
  assert(!controller.update(start + kSleepMs + 6000, input).visible);
  input.blocked = false;
  assert(controller.update(start + kSleepMs + 6020, input).visible);
  input.action = Action::Stop;
  ++input.action_sequence;
  assert(controller.update(start + kSleepMs + 6100, input).mood == Mood::Happy);
}

static std::array<uint16_t, kSize * kSize> frame(const Pose &pose) {
  Renderer renderer;
  renderer.prepare(pose);
  std::array<uint16_t, kSize * kSize> pixels{};
  for (int top = 0; top < kSize; top += kTileRows) {
    const int rows = std::min(kTileRows, kSize - top);
    assert(renderer.render_tile(top, rows, pixels.data() + top * kSize,
                                pixels.size() - top * kSize));
  }
  return pixels;
}

static void render_tests() {
  Renderer renderer;
  renderer.prepare(Pose{});
  std::array<uint16_t, kSize * kTileRows + 2> guarded{};
  guarded.front() = guarded.back() = 0xbeef;
  assert(!renderer.render_tile(-1, 1, guarded.data() + 1, guarded.size() - 2));
  assert(!renderer.render_tile(150, 48, guarded.data() + 1, guarded.size() - 2));
  assert(!renderer.render_tile(0, 48, guarded.data() + 1, 12));
  assert(!renderer.render_tile(0, 49, guarded.data() + 1, guarded.size() - 2));
  assert(!renderer.render_tile(0, 1, nullptr, 0));
  assert(renderer.render_tile(0, kTileRows, guarded.data() + 1, guarded.size() - 2));
  assert(guarded.front() == 0xbeef && guarded.back() == 0xbeef);
  const auto pixels = frame(Pose{});
  const auto ink = std::count_if(pixels.begin(), pixels.end(), [](uint16_t p) { return p != 0; });
  assert(ink > 10000 && ink < 14000);
  Pose invisible;
  invisible.opacity = 0;
  const auto black = frame(invisible);
  assert(std::all_of(black.begin(), black.end(), [](uint16_t p) { return p == 0; }));

  Controller controller;
  Input input;
  // Variable refresh intervals and long stalls, across the 32-bit clock wrap.
  uint32_t now = 0xffff0000U;
  for (int i = 0; i < 1200; ++i) {
    now += i % 50 == 0 ? 1200 : 40;
    if (i % 100 == 0) {
      ++input.action_sequence;
      input.action = i % 200 ? Action::Previous : Action::Next;
    }
    auto pose = controller.update(now, input);
    assert(std::isfinite(pose.angle) && std::isfinite(pose.lid));
    assert(pose.scale_x > 0.9f && pose.scale_x < 1.1f);
    const auto rendered = frame(pose);
    for (int x = 0; x < kSize; ++x) {
      assert(rendered[x] == 0 && rendered[(kSize - 1) * kSize + x] == 0);
      assert(rendered[x * kSize] == 0 && rendered[x * kSize + kSize - 1] == 0);
    }
  }
}

static void feedback_tests() {
  Controller controller;
  Input input;
  input.playing = true;
  controller.update(0, input);
  assert(controller.update(1000, input).visible);
  assert(controller.update(1000, input).mood == Mood::Humming);
  input.feedback = Feedback::Hold;
  input.hold_percent = 50;
  Pose pose;
  for (uint32_t now = 1020; now <= 2020; now += 20)
    pose = controller.update(now, input);
  assert(pose.visible && pose.mood == Mood::Progress && pose.progress_morph > 0.99f);
  assert(std::abs(pose.ring_sweep - 3.14159265f) < 0.001f);
  auto pixels = frame(pose);
  assert(pixels[78 * kSize + 80] == 0); // A hollow ring, not a filled disk.
  assert(pixels[78 * kSize + 123] > pixels[78 * kSize + 37]);
  input.blocked = true;
  assert(!controller.update(2040, input).visible); // Real menu always owns screen.
  input.blocked = false;
  input.feedback = Feedback::Connecting;
  pose = controller.update(2060, input);
  const float start = pose.ring_start;
  pose = controller.update(2600, input);
  assert(pose.ring_start != start && pose.ring_sweep < 6.28f);
  input.feedback = Feedback::Success;
  assert(controller.update(2620, input).mood == Mood::Happy);
  assert(controller.update(4500, input).visible); // Playback retains Pebble.
  input.feedback = Feedback::Error;
  assert(controller.update(4520, input).mood == Mood::Confused);
  input.feedback = Feedback::None;
  assert(controller.update(4540, input).visible);
  input.feedback = Feedback::Radar;
  const auto radar_start = controller.update(4600, input);
  const auto radar_next = controller.update(5000, input);
  assert(radar_next.visible && radar_next.mood == Mood::Radar);
  assert(frame(radar_start) != frame(radar_next));
  assert(frame(radar_start) == frame(controller.update(5900, input))); // Source's 1300 ms period.
  const auto radar_pixels = frame(radar_next);
  assert(radar_pixels[80 * kSize + 80] != 0); // Solid center, no hollow sweep origin.
  for (int y = 0; y < kSize; ++y)
    for (int x = 0; x < kSize; ++x)
      if (x < 32 || x >= 128 || y < 32 || y >= 128)
        assert(radar_pixels[y * kSize + x] == 0);
  input.feedback = Feedback::Error;
  assert(controller.update(5040, input).mood == Mood::Confused);
  // Exercise morphs, full circles and cancellation at each hold percentage.
  for (int n = 0; n <= 100; ++n) {
    pose.progress_morph = n / 100.0f;
    pose.ring_sweep = n / 100.0f * 6.2831853f;
    pose.opacity = 1;
    pixels = frame(pose);
    for (int x = 0; x < kSize; ++x)
      assert(pixels[x] == 0 && pixels[(kSize - 1) * kSize + x] == 0);
  }
}

static void interaction_tests() {
  for (uint32_t start : {0U, 0xfffffff0U}) {
    Controller controller;
    Input input;
    input.playing = true;
    assert(controller.update(start, input).mood == Mood::Humming);
    input.action = Action::Volume; ++input.action_sequence; input.volume = 52;
    assert(controller.update(start + 100, input).mood == Mood::Listening);
    ++input.action_sequence; input.volume = 56;
    controller.update(start + 180, input);
    ++input.action_sequence; input.volume = 60;
    assert(controller.update(start + 260, input).mood == Mood::Excited);
    ++input.action_sequence; input.volume = 70;
    assert(controller.update(start + 340, input).mood == Mood::Surprised);
    ++input.action_sequence;
    assert(controller.update(start + 500, input).mood == Mood::Surprised);
    ++input.action_sequence; // Repeated turns at maximum do not restart the surprise.
    assert(controller.update(start + 1300, input).mood == Mood::Humming);
    input.volume = 68; ++input.action_sequence;
    assert(controller.update(start + 1400, input).mood == Mood::Listening);
    input.action = Action::Previous; ++input.action_sequence;
    auto pose = controller.update(start + 1500, input);
    for (int i=1; i<12; ++i) pose=controller.update(start + 1500+i*40, input);
    assert(pose.mood == Mood::Playful && pose.gaze_x < 0);
    input.action = Action::Next; ++input.action_sequence;
    for (int i=0; i<15; ++i) pose=controller.update(start + 2000+i*40, input);
    assert(pose.mood == Mood::Playful && pose.gaze_x > 0);
    input.feedback = Feedback::Hold;
    assert(controller.update(start + 2640, input).mood == Mood::Progress);
    input.blocked = true;
    assert(!controller.update(start + 2680, input).visible);
  }
}

static void motion_tests() {
  Controller controller;
  Input input;
  auto pose = controller.update(0, input);
  const float start_x = pose.x;
  const float start_y = pose.y;
  input.motion_valid = true;
  // User-facing convention: left/right tilt is the sensor X axis and moves
  // the pet horizontally on screen.
  input.motion_tilt_x = 25;
  bool hit_right = false, right_shape = false;
  for (uint32_t now = 33; now <= 3300; now += 33) {
    pose = controller.update(now, input);
    hit_right = hit_right || (pose.edge_contact == EdgeContact::Right &&
                              pose.edge_impact > 0);
    right_shape = right_shape || (pose.edge_contact == EdgeContact::Right &&
                                  pose.scale_x < 1 && pose.scale_y > 1);
  }
  assert(pose.x > start_x + 4);
  assert(hit_right);
  assert(right_shape);

  input.motion_tilt_x = 0;
  for (uint32_t now = 3333; now <= 7000; now += 33)
    pose = controller.update(now, input);
  assert(std::abs(pose.x - start_x) < 4);
  assert(pose.edge_impact < 0.05f);

  Controller vertical;
  Input up;
  up.motion_valid = true;
  // User-facing convention: front/back tilt is the sensor Y axis and moves
  // the pet vertically on screen.
  up.motion_tilt_y = -25;
  bool hit_top = false, top_shape = false;
  for (uint32_t now = 33; now <= 3300; now += 33) {
    pose = vertical.update(now, up);
    hit_top = hit_top || (pose.edge_contact == EdgeContact::Top &&
                          pose.edge_impact > 0);
    top_shape = top_shape || (pose.edge_contact == EdgeContact::Top &&
                              pose.scale_y < 1 && pose.scale_x > 1);
  }
  assert(pose.y < start_y - 4);
  assert(hit_top);
  assert(top_shape);

  Controller shaken;
  Input shake;
  shake.motion_valid = true;
  shake.motion_angular_speed_dps = 300;
  const auto before_shake = shaken.update(0, shake);
  const auto after_shake = shaken.update(700, shake);
  assert(after_shake.y < before_shake.y);
  const auto held_shake = shaken.update(733, shake);
  assert(std::abs(held_shake.y - after_shake.y) < 8);

  input.blocked = true;
  const auto blocked = controller.update(7033, input);
  assert(blocked.visible == false);
}

// Optional preview uses the exact firmware renderer, with no alternate drawing.
static void write_frame(const std::string &path, const Pose &pose) {
  const auto pixels = frame(pose);
  std::ofstream out(path, std::ios::binary);
  out << "P6\n160 160\n255\n";
  for (uint16_t p : pixels) {
    const char rgb[] = {static_cast<char>(((p >> 11) & 31) * 255 / 31),
                        static_cast<char>(((p >> 5) & 63) * 255 / 63),
                        static_cast<char>((p & 31) * 255 / 31)};
    out.write(rgb, 3);
  }
  assert(out.good());
}

int main(int argc, char **argv) {
  for (uint32_t start : {0U, 0xffff0000U}) {
    Controller c; Input in; in.playing=true;
    std::array<bool, 32> seen{};
    for (uint32_t ms=0; ms<95000; ms+=33) {
      auto p=c.update(start+ms,in);
      seen[static_cast<size_t>(p.mood)]=true;
      assert(p.visible && std::isfinite(p.x) && std::isfinite(p.angle));
      assert(p.particle_count==0); // Ambient cycle never sustains confetti.
    }
    for (auto mood : {Mood::Humming,Mood::Listening,Mood::Happy,Mood::Playful,Mood::Excited,Mood::Curious})
      assert(seen[static_cast<size_t>(mood)]);
    in.action=Action::Mode;++in.action_sequence;
    auto p=c.update(start+95000,in);assert(p.particle_count==kParticleLimit);
    size_t colored_outside_body=0;
    for (uint32_t ms=95033;ms<95900;ms+=33) {
      p=c.update(start+ms,in);assert(p.particle_count<=kParticleLimit);
      const auto confetti=frame(p);auto bare=p;bare.particle_count=0;const auto body=frame(bare);
      for(size_t i=0;i<confetti.size();++i) if(confetti[i]!=body[i]) ++colored_outside_body;
    }
    assert(colored_outside_body>0);
    assert(c.update(start+96000,in).particle_count==0);
    in.action=Action::Play;++in.action_sequence;c.update(start+97000,in);
    in.blocked=true;assert(c.update(start+97033,in).particle_count==0);
    in.blocked=false;in.action=Action::Volume;++in.action_sequence;
    assert(c.update(start+98000,in).mood==Mood::Listening);
    in.feedback=Feedback::Radar;assert(c.update(start+98033,in).mood==Mood::Radar);
    in.feedback=Feedback::None;in.playing=false;c.update(start+100000,in);
    in.playing=true;assert(c.update(start+100033,in).mood==Mood::Humming);
  }
  state_tests(0);
  {
    Controller quiet, loud; Input a, b; a.playing = b.playing = true; b.music_level = 1;
    Pose qa, qb;
    for (uint32_t ms=0; ms<=1980; ms+=33) { qa=quiet.update(ms,a); qb=loud.update(ms,b); }
    assert(qb.y < qa.y - 1 && qb.scale_y > qa.scale_y);
    a.feedback=b.feedback=Feedback::Hold;
    for (uint32_t ms=2013; ms<4000; ms+=33) { qa=quiet.update(ms,a); qb=loud.update(ms,b); }
    assert(std::abs(qa.y-qb.y)<.01f && qa.mood==Mood::Progress && qb.mood==Mood::Progress);
    b.music_level=NAN;b.feedback=Feedback::None;
    assert(std::isfinite(loud.update(4030,b).y));
  }
  state_tests(0xffff0000U);
  render_tests();
  feedback_tests();
  interaction_tests();
  motion_tests();
  {
    Controller c; Input in; c.update(0, in);
    in.feedback = Feedback::Uploading; in.hold_percent = 42;
    auto p = c.update(40, in);
    assert(p.visible && p.mood == Mood::Progress && std::abs(p.ring_sweep - 0.42f * 2 * 3.14159265f) < 0.001f);
    in.feedback = Feedback::None; in.volume = 55;
    p = c.update(80, in); assert(p.volume_visible && p.volume_opacity == 1);
    p = c.update(1400, in); assert(p.volume_visible && p.volume_opacity > 0 && p.volume_opacity < 1);
    p = c.update(1600, in); assert(!p.volume_visible);
  }
  if (argc == 2) {
    Controller music;Input playing;playing.playing=true;
    for(uint32_t ms=0;ms<=47000;ms+=33) {
      if(ms==33){playing.action=Action::Mode;++playing.action_sequence;}
      const auto pose=music.update(ms,playing);
      if(ms%330==0 || (ms>=33 && ms<=900)) {
        char name[60];std::snprintf(name,sizeof(name),"/music-%05u.ppm",ms);
        write_frame(std::string(argv[1])+name,pose);
      }
    }
    Controller controller;
    Input input;
    for (int i = 0; i < 200; ++i) {
      if (i == 55) { ++input.action_sequence; input.action = Action::Volume; }
      if (i == 110) { ++input.action_sequence; input.action = Action::Stop; }
      char suffix[40];
      std::snprintf(suffix, sizeof(suffix), "/frame-%03d.ppm", i);
      write_frame(std::string(argv[1]) + suffix, controller.update(i * 40, input));
    }
    for (int i = 0; i < 40; ++i) controller.update(kSleepMs + 10000 + i * 40, input);
    write_frame(std::string(argv[1]) + "/sleep.ppm", controller.update(kSleepMs + 12000, input));
    Controller progress;
    Input feedback;
    for (int i = 0; i < 280; ++i) {
      feedback.feedback = i < 35 ? Feedback::None : i < 73 ? Feedback::Hold :
          i < 95 ? Feedback::None : i < 185 ? Feedback::Connecting :
          i < 240 ? Feedback::Success : Feedback::Error;
      feedback.hold_percent = static_cast<uint8_t>(std::clamp((i-35)*100/37, 0, 100));
      char suffix[40];
      std::snprintf(suffix, sizeof(suffix), "/progress-%03d.ppm", i);
      write_frame(std::string(argv[1]) + suffix, progress.update(i*40, feedback));
    }
    feedback.feedback = Feedback::Radar;
    for (int i = 0; i < 80; ++i) {
      char suffix[40];
      std::snprintf(suffix, sizeof(suffix), "/radar-%03d.ppm", i);
      write_frame(std::string(argv[1]) + suffix, progress.update(12000+i*40, feedback));
    }
    Controller interactions;
    Input action;
    action.playing = true;
    for (int i = 0; i < 220; ++i) {
      if (i == 40) { action.action = Action::Volume; ++action.action_sequence; action.volume = 52; }
      if (i == 46) { ++action.action_sequence; action.volume = 60; }
      if (i == 60) { ++action.action_sequence; action.volume = 70; }
      if (i == 95) { action.action = Action::Previous; ++action.action_sequence; }
      if (i == 135) { action.action = Action::Next; ++action.action_sequence; }
      char suffix[40];
      std::snprintf(suffix, sizeof(suffix), "/interaction-%03d.ppm", i);
      write_frame(std::string(argv[1]) + suffix, interactions.update(i*40, action));
    }
  }
  std::puts("character: states, feedback priority, ring geometry, wraparound, stalls and tile bounds passed");
}
