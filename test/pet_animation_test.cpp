#include "smart_speaker_pet_animation.h"
#include "smart_speaker_pet_shake.h"
#include "smart_speaker_touch_input.h"

#include <array>
#include <cassert>
#include <iostream>

using namespace smart_speaker;
using namespace smart_speaker::product;

int main() {
  constexpr PetTouchTarget hit{70, 70, 100, 100};
  TouchInput input;
  PetAnimation pet;
  character::Pose pose;
  auto step = [&](uint32_t now, bool active = true, PetClip voice = PetClip::Count,
                  PetClip loaded = PetClip::Idle, unsigned index = 0) {
    return pet.update(now, active, active && voice == PetClip::Count, voice,
                      input.feedback(), loaded, index, 12, pose, 160);
  };
  const auto idle = step(0);
  assert(idle.clip == PetClip::Idle && idle.sprite.x == 20 && idle.sprite.y == 20);
  assert(step(100).index == 1);
  pose.x = 10;
  assert(step(110).sprite.x > idle.sprite.x); // The existing IMU pose moves the sprite.
  pose.x = 0;

  assert(input.update(120, true, 110, 110, hit) == TouchGesture::None);
  assert(input.update(160, false, 110, 110, hit) == TouchGesture::None);
  const auto tapped = step(160);
  assert(tapped.clip == PetClip::Tap && tapped.sprite.h < idle.sprite.h);
  assert(step(800).index == 0); // Late SD frame doesn't consume the beginning.
  assert(step(900, true, PetClip::Count, PetClip::Tap).index == 0);
  assert(step(1020, true, PetClip::Count, PetClip::Tap).index == 1);
  assert(step(1901, true, PetClip::Count, PetClip::Tap).clip == PetClip::Idle);

  input.update(2000, true, 110, 110, hit);
  input.update(2020, true, 110, 131, hit);
  const auto drag = step(2020);
  assert(drag.clip == PetClip::Drag && drag.sprite.y > idle.sprite.y);
  assert(input.update(2040, false, 110, 131, hit) == TouchGesture::None);
  assert(step(2040).sprite.y == drag.sprite.y);
  assert(step(2250).sprite.y < idle.sprite.y); // Overshoots on return.
  assert(step(2530).sprite.y == idle.sprite.y);

  const auto point = [&](uint32_t now, bool down, unsigned raw_x, unsigned raw_y) {
    return input.update(now, down, screen_touch_axis(raw_x), screen_touch_axis(raw_y), hit);
  };
  point(2600, true, 129, 129);
  point(2620, true, 129, 109); // Physical down -> larger screen Y.
  assert(step(2620).sprite.y > idle.sprite.y);
  point(2640, true, 187, 109); // Physical left -> smaller screen X.
  assert(input.feedback().dragging && input.feedback().dx == -58 && input.feedback().dy == 20);
  assert(step(2640).sprite.x < idle.sprite.x);
  assert(point(2660, false, 187, 109) == TouchGesture::None);
  assert(input.feedback().taps == 1 && input.feedback().releases == 2);
  assert(step(2660, false).clip == PetClip::Count);
  assert(step(2700).clip == PetClip::Idle);
  point(2720, true, 89, 219); // Starts on wallpaper, outside the sprite.
  point(2740, true, 154, 219);
  assert(point(2760, false, 154, 219) == TouchGesture::SwipeLeft);

  input.update(2800, true, 110, 110, hit);
  input.update(2820, true, 110, 130, hit);
  assert(input.update(2840, false, 110, 130, hit, false) == TouchGesture::None);
  assert(!input.feedback().dragging && input.feedback().releases == 2);
  input.update(2860, true, 110, 130, hit);
  assert(input.update(2880, false, 110, 130, hit) == TouchGesture::None);
  assert(input.feedback().taps == 1);

  input.update(3000, true, 110, 110, hit);
  assert(input.update(3920, true, 110, 110, hit) == TouchGesture::None);
  assert(step(3920, true, PetClip::Think).clip == PetClip::Think);
  assert(input.update(3940, true, 110, 110, {}) == TouchGesture::None);
  assert(input.update(4000, false, 110, 110, {}) == TouchGesture::None);
  assert(step(4100, true, PetClip::Talk).clip == PetClip::Talk);
  assert(step(4200).clip == PetClip::Idle);

  // Holding a pet never starts voice and still permits a later drag.
  TouchInput held;
  held.update(0, true, 110, 110, hit);
  assert(held.update(1000, true, 110, 110, hit) == TouchGesture::None);
  held.update(1050, true, 145, 110, hit);
  assert(held.feedback().dragging && held.feedback().dx == 35);
  held.cancel();
  assert(!held.feedback().dragging);
  held.update(1100, true, 180, 110, hit);
  assert(held.update(1150, false, 180, 110, hit) == TouchGesture::None);
  assert(held.feedback().releases == 0 && held.feedback().taps == 0);

  auto scripted = [&](uint32_t now, PetClip loaded, int action = -1, PetClip voice = PetClip::Count,
                      bool visible = true) {
    return pet.update(now, visible, visible && voice == PetClip::Count, voice, input.feedback(),
                      loaded, 0, 12, pose, 160, action, {}, true);
  };
  const auto last = static_cast<PetClip>(kPetClipCount - 1);
  assert(scripted(4300, PetClip::Idle, int(last)).clip == last);
  assert(scripted(4600, PetClip::Idle).index == 0);
  assert(scripted(4700, last).index == 0);
  assert(scripted(5700, last).clip == PetClip::Idle);
  assert(scripted(5800, PetClip::Idle, kPetToggleSleep).clip == PetClip::SleepStart);
  assert(scripted(5850, PetClip::SleepStart).index == 0);
  assert(scripted(6850, PetClip::SleepStart).clip == PetClip::SleepLoop);
  assert(scripted(6900, PetClip::SleepLoop).index == 0);
  assert(scripted(7000, PetClip::SleepLoop, -1, PetClip::Talk).clip == PetClip::Talk);
  assert(scripted(7300, PetClip::Talk).clip == PetClip::SleepLoop);
  assert(scripted(7400, PetClip::SleepLoop, -1, PetClip::Count, false).clip == PetClip::Count);
  assert(scripted(7500, PetClip::Idle).clip == PetClip::SleepLoop);
  input.update(7600, true, 110, 110, hit);
  input.update(7650, false, 110, 110, hit);
  assert(scripted(7650, PetClip::SleepLoop).clip == PetClip::SleepEnd);
  assert(scripted(7700, PetClip::SleepEnd).index == 0);
  assert(scripted(8700, PetClip::SleepEnd).clip == PetClip::Idle);
  assert(scripted(8800, PetClip::Idle, kPetToggleSleep).clip == PetClip::SleepStart);
  assert(scripted(8900, PetClip::SleepStart, kPetToggleSleep).clip == PetClip::Idle);
  assert(scripted(9000, PetClip::Idle).clip == PetClip::Idle);
  const auto ambient = scripted(69000, PetClip::Idle);
  assert(ambient.clip > PetClip::SleepEnd && ambient.clip < PetClip::Count);

  TouchInput fling;
  fling.update(0, true, 110, 100, hit);
  fling.update(20, true, 110, 114, hit);
  fling.update(40, true, 110, 129, hit);
  fling.update(60, false, 110, 129, hit);
  assert(fling.feedback().release_vy > 200 && !fling.feedback().release_sling);
  fling.update(200, true, 110, 100, hit);
  fling.update(230, false, 110, 100, hit);
  fling.update(300, true, 111, 100, hit);
  fling.update(320, true, 110, 117, hit);
  fling.update(430, false, 110, 117, hit);
  assert(fling.feedback().release_vy == 0 && fling.feedback().release_sling);

  PetShake shake;
  for (unsigned i = 0; i < 20; ++i) shake.sample(i, i * 20, i * .02f, 0, 1);
  assert(shake.count == 0);
  shake.sample(21, 420, 1.3f, 0, 1);
  shake.sample(21, 420, -1.3f, 0, 1);
  assert(shake.count == 0); // A repeated sensor snapshot isn't a new peak.
  shake.sample(22, 440, -1.3f, 0, 1);
  assert(shake.count == 1 && shake.direction_x == -1);
  shake.sample(23, 460, 1.3f, 0, 1);
  shake.sample(24, 480, -1.3f, 0, 1);
  assert(shake.count == 1);

  std::array<uint16_t, kPetSpritePixels> source{};
  source.fill(0xff12);
  std::array<uint16_t, 160 * 16> tile;
  tile.fill(0xffff);
  render_pet_sprite_tile(source.data(), {20, 20, 120, 120}, 16, 16, tile.data());
  assert(tile[0] == 0 && tile[4 * 160 + 19] == 0 && tile[4 * 160 + 20] == 0xff12);
  assert(tile[4 * 160 + 139] == 0xff12 && tile[4 * 160 + 140] == 0);
  render_pet_sprite_tile(source.data(), {20, 40, 120, 120}, 16, 16, tile.data());
  for (const auto px : tile) assert(px == 0); // Previous position leaves no trails.
  std::cout << "dsh-pet: point playback, sleep, ambient, touch fling/slingshot, IMU shake, swipe priority, voice and tile damage passed\n";
}
