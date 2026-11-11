#include "smart_speaker_home_pet.h"
#include "smart_speaker_launcher.h"
// Explicit fixtures for host visual QA, never linked into the firmware.
#include "smart_speaker_home.h"
#include "smart_speaker_boot.h"
#include "assets/home_clock_font.h"
#include "lunaredu_lunaros/network/http_client.h"
#include "lunaredu_lunaros/network/network_jobs.h"
#include <array>
#include <chrono>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <iterator>
#include <vector>

// The production weather label mapping is linked for this preview. Any
// accidental attempt to start its network service is a hard test failure.
namespace lunaredu::lunaros {
bool submit_network_job(const NetworkJobDefinition &, NetworkJobHandle &, std::string &) { assert(false); return false; }
bool network_job_active(NetworkJobHandle) { assert(false); return false; }
bool network_job_cancellation_requested(NetworkJobHandle) { assert(false); return true; }
bool network_fetch_resource(NetworkFetchRequest, NetworkResponse *) { assert(false); return false; }
}

using namespace smart_speaker;
namespace {
std::array<uint16_t, 240 * 240> framebuffer{};
alignas(8) std::array<uint16_t, 240 * 16> draw_buffer{};
uint64_t flushed_pixels = 0;
uint32_t flush_calls = 0;
void flush(lv_display_t *display, const lv_area_t *area, uint8_t *bytes) {
  flushed_pixels += lv_area_get_width(area) * lv_area_get_height(area);
  ++flush_calls;
  auto *pixels = reinterpret_cast<uint16_t *>(bytes);
  for (int y = area->y1; y <= area->y2; ++y)
    for (int x = area->x1; x <= area->x2; ++x)
      framebuffer[y * 240 + x] = *pixels++;
  lv_display_flush_ready(display);
}
void bounds(lv_obj_t *object) {
  if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return;
  lv_area_t box;
  lv_obj_get_coords(object, &box);
  if (box.x1 < 0 || box.y1 < 0 || box.x2 >= 240 || box.y2 >= 240)
    std::cerr << "Out of bounds: " << box.x1 << ',' << box.y1 << ' ' << box.x2 << ',' << box.y2
              << (lv_obj_check_type(object, &lv_label_class) ? lv_label_get_text(object) : " object") << '\n';
  assert(box.x1 >= 0 && box.y1 >= 0 && box.x2 < 240 && box.y2 < 240);
  if (lv_obj_check_type(object, &lv_label_class)) {
    const auto *font = lv_obj_get_style_text_font(object, LV_PART_MAIN);
    const char *value = lv_label_get_text(object);
    if (std::strlen(value) == 2 && std::strspn(value, "0123456789-") == 2) {
      lv_point_t measured{};
      lv_text_get_size(&measured, value, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      assert(measured.x <= lv_obj_get_width(object));
    }
    const auto *p = reinterpret_cast<const unsigned char *>(lv_label_get_text(object));
    while (*p) {
      uint32_t cp = *p++;
      if (cp >= 0xC0) {
        const int count = cp < 0xE0 ? 1 : cp < 0xF0 ? 2 : 3;
        cp &= count == 1 ? 0x1F : count == 2 ? 0x0F : 0x07;
        for (int i = 0; i < count; ++i) cp = (cp << 6) | (*p++ & 0x3F);
      }
      lv_font_glyph_dsc_t glyph{};
      assert(lv_font_get_glyph_dsc(font, &glyph, cp, 0));
    }
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
    bounds(lv_obj_get_child(object, i));
}
void save(const std::filesystem::path &path) {
  lv_obj_update_layout(lv_screen_active());
  lv_obj_update_layout(lv_layer_top());
  bounds(lv_screen_active());
  bounds(lv_layer_top());
  lv_refr_now(lv_display_get_default());
  std::ofstream output(path, std::ios::binary);
  output << "P6\n240 240\n255\n";
  for (auto p : framebuffer) {
    const char rgb[] = {static_cast<char>(((p >> 11) & 31) * 255 / 31),
        static_cast<char>(((p >> 5) & 63) * 255 / 63), static_cast<char>((p & 31) * 255 / 31)};
    output.write(rgb, sizeof(rgb));
  }
  assert(output.good());
}

// Exercise a clock that first gets valid time after the boot overlay is gone.
// Repeat the same transition to compare cold and warm draws at equal work.
auto check_first_clock(lv_display_t *display, SpeakerHome &home, const char *font_name,
                       const std::filesystem::path &output) {
  product::WeatherSnapshot weather;
  std::tm local{};
  local.tm_hour = 17; local.tm_min = 46; local.tm_sec = 28;
  const auto pending = home_content(nullptr, weather, false);
  const auto synced = home_content(&local, weather, false);
  const bool save_frames = std::strcmp(font_name, "bitmap") == 0;
  home.show(pending, true);
  lv_refr_now(display);
  if (save_frames) save(output / "clock-before-sync.ppm");
  const auto placeholder = framebuffer;
  auto refresh = [&] {
    flushed_pixels = 0;
    home.show(synced, true);
    lv_obj_update_layout(lv_screen_active());
    const auto start = std::chrono::steady_clock::now();
    lv_refr_now(display);
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
  };
  const auto first_us = refresh();
  const auto first_pixels = flushed_pixels;
  assert(framebuffer != placeholder);
  const auto valid = framebuffer;
  if (save_frames) save(output / "clock-after-sync.ppm");
  home.show(pending, true);
  lv_refr_now(display);
  assert(framebuffer == placeholder);
  const auto repeat_us = refresh();
  assert(framebuffer == valid && flushed_pixels == first_pixels);
  std::cout << font_name << " first valid clock: " << first_pixels << " pixels, " << first_us
            << " us; warm repeat: " << repeat_us << " us\n";
  return std::pair(placeholder, valid);
}

void check_clock_font(const lv_font_t *reference, const lv_font_t *bitmap) {
  assert(lv_font_has_static_bitmap(bitmap));
  assert(reference->line_height == bitmap->line_height && reference->base_line == bitmap->base_line);
  for (const unsigned char ch : std::string("-0123456789:")) {
    lv_font_glyph_dsc_t old_glyph{}, new_glyph{};
    assert(lv_font_get_glyph_dsc(reference, &old_glyph, ch, 0) && !old_glyph.is_placeholder);
    assert(lv_font_get_glyph_dsc(bitmap, &new_glyph, ch, 0) && !new_glyph.is_placeholder);
    assert(old_glyph.adv_w == new_glyph.adv_w && old_glyph.box_w == new_glyph.box_w &&
           old_glyph.box_h == new_glyph.box_h && old_glyph.ofs_x == new_glyph.ofs_x &&
           old_glyph.ofs_y == new_glyph.ofs_y && old_glyph.format == new_glyph.format);
    const auto *old_mask = static_cast<const lv_draw_buf_t *>(lv_font_get_glyph_bitmap(&old_glyph, nullptr));
    const auto *new_mask = static_cast<const uint8_t *>(lv_font_get_glyph_static_bitmap(&new_glyph));
    assert(old_mask && new_mask);
    for (unsigned y = 0; y < old_glyph.box_h; ++y)
      assert(std::memcmp(old_mask->data + y * old_mask->header.stride,
                         new_mask + y * new_glyph.stride, old_glyph.box_w) == 0);
    lv_font_glyph_release_draw_data(&old_glyph);
  }
}
}
int main(int argc, char **argv) {
  if (argc != 4 && argc != 5) { std::cerr << "Usage: home_preview CHINESE.ttf DIGITS.ttf OUTPUT_DIR [HOME_PET_IDLE.bin]\n"; return 2; }
  const std::filesystem::path output = argv[3];
  std::filesystem::create_directories(output);
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> font_bytes((std::istreambuf_iterator<char>(input)), {});
  assert(!font_bytes.empty());
  std::ifstream digit_input(argv[2], std::ios::binary);
  std::vector<uint8_t> digit_bytes((std::istreambuf_iterator<char>(digit_input)), {});
  assert(!digit_bytes.empty());
  lv_init();
  auto *display = lv_display_create(240, 240);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, draw_buffer.data(), nullptr, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);
  std::array<lv_font_t *, 6> fonts{};
  constexpr int sizes[] = {100, 42, 16, 14, 12, 20};
  for (size_t i = 0; i < fonts.size(); ++i) {
    const auto &bytes = i < 2 ? digit_bytes : font_bytes;
    fonts[i] = lv_tiny_ttf_create_data_ex(bytes.data(), bytes.size(), sizes[i], LV_FONT_KERNING_NONE, i < 2 ? 12 : 40);
    assert(fonts[i]);
  }
  auto make_wallpaper = [] {
    auto *wallpaper = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(wallpaper);
    lv_obj_set_size(wallpaper, 240, 240);
    lv_obj_set_style_bg_color(wallpaper, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(wallpaper, LV_OPA_COVER, 0);
  };
  make_wallpaper();
  SpeakerHome reference;
  reference.create(lv_screen_active(), fonts[0], fonts[1], fonts[2], fonts[4], fonts[5]);
  const auto ttf_frames = check_first_clock(display, reference, "TinyTTF", output);
  lv_obj_clean(lv_screen_active());
  make_wallpaper();
  SpeakerHome home;
  home.create(lv_screen_active(), &speaker_home_digits, &speaker_home_seconds, fonts[2], fonts[4], fonts[5]);
  assert(check_first_clock(display, home, "bitmap", output) == ttf_frames);
  check_clock_font(fonts[0], &speaker_home_digits);
  check_clock_font(fonts[1], &speaker_home_seconds);
  std::tm local{};
  local.tm_year = 126; local.tm_mon = 10; local.tm_mday = 11;
  local.tm_wday = 3; local.tm_hour = 11; local.tm_min = 4; local.tm_sec = 54;
  product::WeatherSnapshot weather;
  weather.config.enabled = true; weather.config.place = "杭州";
  weather.has_reading = true; weather.reading.temperature_c = 24;
  weather.reading.humidity_percent = 57;
  weather.reading.code = 2; weather.status = "fresh"; weather.age_seconds = 180;
  auto content = home_content(&local, weather, false);
  assert(content.time == "11:04" && content.date == "11月11日" && content.weekday == "周三");
  assert(content.temperature == "24°C" && content.status.empty());
  assert(content.humidity == "57%");
  home.show(content, false); save(output / "home-clock.ppm");
  home.show(content, true); save(output / "home-bright-background.ppm");
  local.tm_hour = 0; local.tm_min = 0; local.tm_sec = 0;
  home.show(home_content(&local, weather, false), false); save(output / "home-midnight.ppm");
  local.tm_hour = 23; local.tm_min = 59; local.tm_sec = 59;
  home.show(home_content(&local, weather, false), false); save(output / "home-late-night.ppm");
  // The supplied snow reference is an explicit fixture, not live weather.
  weather.reading.code = 73;
  weather.reading.temperature_c = -2; weather.reading.humidity_percent = 83;
  local.tm_hour = 13; local.tm_min = 1; local.tm_sec = 39;
  home.show(home_content(&local, weather, false), false); save(output / "home-snow-reference.ppm");
  weather.reading.code = 2;
  weather.config.place = "中华人民共和国北京市海淀区";
  weather.status = "offline"; weather.age_seconds = 7200;
  weather.reading.temperature_c = -12;
  content = home_content(&local, weather, false);
  assert(content.status.empty() && content.temperature == "-12°C");
  home.show(content, false); save(output / "home-offline-long-place.ppm");
  weather.status = "stale";
  assert(home_content(&local, weather, false).status.empty());
  weather.status = "error";
  assert(home_content(&local, weather, false).status.empty());
  weather.status = "time_untrusted";
  content = home_content(nullptr, weather, false);
  assert(content.time == "--:--" && content.weekday == "未校时" && content.seconds == "--");
  assert(content.status.empty());
  home.show(content, false); save(output / "home-untrusted.ppm");
  weather = {};
  content = home_content(nullptr, weather, false);
  assert(content.temperature == "--°C" && content.humidity == "--%" && content.weather_code == -1);
  home.show(content, false); save(output / "home-unconfigured.ppm");
  content = home_content(&local, weather, true);
  assert(content.status == "背景不可用，请重新选择");
  home.show(content, false); save(output / "home-missing-background.ppm");
  for (int i = 0; i < 100; ++i) { home.hide(); home.show(content, false); }
  assert(lv_obj_get_child_count(lv_screen_active()) == 2);

  // The same clock objects participate in the opening and the final home.
  SpeakerBootScreen boot;
  const auto top_children = lv_obj_get_child_count(lv_layer_top());
  const auto home_children = lv_obj_get_child_count(lv_screen_active());
  auto tick = [&](uint32_t ms, BootNetworkStatus network, bool home_target = true) {
    const bool active = boot.update(ms, network, home_target);
    home.show(content, false);
    return active;
  };
  home.show(content, false); save(output / "boot-reference-home.ppm");
  const auto home_frame = framebuffer;
  boot.create(lv_layer_top(), home, "1.0.35", 0);
  assert(tick(0, {})); save(output / "boot-line-start.ppm");
  assert(tick(380, {})); save(output / "boot-line.ppm");
  assert(tick(800, {})); save(output / "boot-clock-enter.ppm");
  assert(tick(1800, {true, false, false})); save(output / "boot-syncing.ppm");
  assert(!boot.revealing());
  assert(tick(2200, {true, true, false})); save(output / "boot-ready.ppm");
  assert(boot.revealing());
  const auto ready_frame = framebuffer;
  assert(tick(2600, {true, true, false})); save(output / "boot-reveal-middle.ppm");
  assert(framebuffer != ready_frame && framebuffer != home_frame);
  assert(tick(3599, {true, true, false}));
  assert(!tick(3600, {true, true, false}));
  save(output / "boot-reveal-end.ppm");
  assert(framebuffer == home_frame); // No endpoint jump or accumulated offsets.
  assert(!tick(4000, {})); // Reconnection cannot replay it.
  assert(lv_obj_get_child_count(lv_layer_top()) == top_children);
  assert(lv_obj_get_child_count(lv_screen_active()) == home_children);

  boot.create(lv_layer_top(), home, "1.0.35", 0);
  assert(tick(4499, {}));
  assert(!boot.revealing());
  assert(tick(4500, {})); save(output / "boot-offline.ppm");
  assert(tick(5899, {}));
  assert(!tick(5900, {})); // Bounded even without network/time.
  boot.create(lv_layer_top(), home, "1.0.35", 0);
  assert(tick(4500, {true, false, false})); save(output / "boot-no-time.ppm");
  assert(!tick(5900, {true, false, false}));
  boot.create(lv_layer_top(), home, "1.0.35", 0);
  assert(tick(1500, {false, false, true}, false)); save(output / "boot-setup.ppm");
  assert(!tick(2900, {false, false, true}, false));
  // A saved Pet desktop has no second clock and keeps the cover until its
  // last stage; the direct Pet writer remains the production display owner's.
  home.hide();
  boot.create(lv_layer_top(), home, "1.0.35", 0);
  assert(boot.update(1500, {true, true, false}, false));
  save(output / "boot-pet-cover.ppm");
  assert(framebuffer[64 * 240 + 60] == 0);
  assert(boot.update(2700, {true, true, false}, false));
  assert(!boot.update(2900, {true, true, false}, false));
  home.show(content, false);
  constexpr uint32_t wrap_start = 0xFFFFFF00U;
  boot.create(lv_layer_top(), home, "1.0.35", wrap_start);
  assert(tick(wrap_start + 1500U, {true, true, false}));
  assert(tick(wrap_start + 2899U, {true, true, false}));
  assert(!tick(wrap_start + 2900U, {true, true, false}));
  // Explicit navigation restores every home presentation property immediately,
  // whether cancelled during clock entry, the waiting pose, or final staging.
  for (uint32_t cancel_at : {0U, 600U, 1500U, 1950U, 2750U}) {
    boot.create(lv_layer_top(), home, "1.0.35", 0);
    assert(tick(std::min(cancel_at, 1500U), {true, true, false}));
    assert(tick(cancel_at, {true, true, false}));
    boot.dismiss(); boot.dismiss();
    assert(!boot.active() && !boot.revealing());
    save(output / "boot-cancel.ppm");
    assert(framebuffer == home_frame);
    assert(lv_obj_get_child_count(lv_layer_top()) == top_children);
  }
  for (int i = 0; i < 100; ++i) {
    boot.create(lv_layer_top(), home, "1.0.35", 0);
    tick(800, {});
    boot.dismiss();
    assert(lv_obj_get_child_count(lv_layer_top()) == top_children);
    assert(lv_obj_get_child_count(lv_screen_active()) == home_children);
  }

  // Bright wallpaper cannot leak through the opening. It enters only with
  // the last home stage; cancellation restores its normal contrast treatment.
  home.show(content, true); save(output / "boot-bright-reference.ppm");
  const auto bright_frame = framebuffer;
  boot.create(lv_layer_top(), home, "1.0.35", 0);
  boot.update(1000, {}); home.show(content, true);
  save(output / "boot-bright-waiting.ppm");
  assert(framebuffer[239 * 240] == 0);
  boot.update(1500, {true, true, false}); home.show(content, true);
  boot.update(2550, {true, true, false}); home.show(content, true);
  save(output / "boot-bright-reveal.ppm");
  assert(framebuffer[0] != 0 && framebuffer[239 * 240] == 0 && framebuffer != bright_frame);
  boot.dismiss(); save(output / "boot-bright-end.ppm");
  assert(framebuffer == bright_frame);

  // Render production LVGL every 40ms. Network/time and readings are fixtures.
  weather = {};
  weather.config.enabled = true; weather.config.place = "杭州";
  weather.has_reading = true; weather.status = "fresh";
  weather.reading.temperature_c = 24; weather.reading.humidity_percent = 57;
  weather.reading.code = 2; weather.age_seconds = 180;
  local.tm_hour = 13; local.tm_min = 1; local.tm_sec = 39;
  content = home_content(&local, weather, false);
  boot.create(lv_layer_top(), home, "1.0.35", 0);
  for (uint32_t i = 0; i < 110; ++i) {
    const uint32_t ms = i * 40;
    tick(ms, {ms >= 1600, ms >= 2400, false});
    char name[32];
    std::snprintf(name, sizeof(name), "boot-frame-%03u.ppm", static_cast<unsigned>(i));
    save(output / name);
  }
  assert(!boot.active() && lv_obj_get_child_count(lv_layer_top()) == top_children);
  // Real view/update/renderer seam: count invalidated pixels on each 20ms
  // display poll, with content updated at the production one-second cadence.
  // Host render microseconds are diagnostic only, not ESP32 timing evidence.
  std::ofstream perf(output / "boot-render-work.csv");
  perf << "ms,pixels,flushes,host_render_us\n";
  boot.create(lv_layer_top(), home, "1.0.35", 0);
  uint64_t wait_pixels = 0, total_pixels = 0;
  for (uint32_t ms = 0; ms <= 4000; ms += 20) {
    boot.update(ms, {ms >= 1600, ms >= 2400, false});
    if (ms % 1000 == 0) home.show(content, false);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_update_layout(lv_layer_top());
    flushed_pixels = 0; flush_calls = 0;
    const auto start = std::chrono::steady_clock::now();
    lv_refr_now(display);
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    perf << ms << ',' << flushed_pixels << ',' << flush_calls << ',' << us << '\n';
    total_pixels += flushed_pixels;
    if (ms >= 1200 && ms < 1600) wait_pixels += flushed_pixels;
  }
  std::cout << "Boot render work: total=" << total_pixels
            << " pixels; unchanged waiting pose=" << wait_pixels << " pixels\n";
  perf.flush();
  std::cout.flush();
  assert(wait_pixels == 0 && "An unchanged waiting pose must not redraw the whole display");
  std::ofstream bright_perf(output / "boot-render-work-bright.csv");
  bright_perf << "ms,pixels,flushes\n";
  boot.create(lv_layer_top(), home, "1.0.35", 0);
  uint64_t background_only_pixels = 0;
  for (uint32_t ms = 0; ms <= 4000; ms += 20) {
    boot.update(ms, {ms >= 1600, ms >= 2400, false});
    if (ms % 1000 == 0) home.show(content, true);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_update_layout(lv_layer_top());
    flushed_pixels = 0; flush_calls = 0;
    lv_refr_now(display);
    bright_perf << ms << ',' << flushed_pixels << ',' << flush_calls << '\n';
    if (ms >= 3060 && ms < 3800) {
      background_only_pixels += flushed_pixels;
      const auto incremental = framebuffer;
      lv_obj_invalidate(lv_screen_active());
      lv_refr_now(display);
      assert(framebuffer == incremental && "Wallpaper reveal must invalidate every changed pixel");
    }
  }
  bright_perf.flush();
  std::cout << "Wallpaper entry and overlapping content: " << background_only_pixels << " pixels\n";
  std::cout.flush();
  assert(background_only_pixels < 600000 && "Background entry must not redraw the entire wallpaper per poll");
  if (argc == 5) {
    using namespace smart_speaker;
    lv_obj_clean(lv_screen_active());
    // Explicit test wallpaper, never a product default.
    auto *paper = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(paper);
    lv_obj_set_size(paper, 240, 240);
    lv_obj_set_style_bg_color(paper, lv_color_hex(0x183643), 0);
    lv_obj_set_style_bg_grad_color(paper, lv_color_hex(0x755769), 0);
    lv_obj_set_style_bg_grad_dir(paper, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(paper, LV_OPA_COVER, 0);
    SpeakerHome wallpaper_home;
    wallpaper_home.create(lv_screen_active(), &speaker_home_digits,
        &speaker_home_seconds, fonts[2], fonts[4], fonts[5]);
    wallpaper_home.show(content, true);
    wallpaper_home.finish_boot();
    lv_refr_now(display);
    const auto wallpaper_with_home = framebuffer;
    std::ifstream pet_file(argv[4], std::ios::binary);
    std::vector<uint8_t> idle((std::istreambuf_iterator<char>(pet_file)), {});
    assert(idle.size() == 3 * HomePetView::kFallbackSide * HomePetView::kFallbackSide);
    alignas(8) static std::array<uint8_t, HomePetView::kBufferBytes> pet_buffer{};
    HomePetView pet;
    pet.create(lv_screen_active(), pet_buffer.data());
    pet.show({160, 145, 80, 80}, nullptr, -1, idle.data());
    save(output / "wallpaper-home.ppm");
    assert(framebuffer[145 * 240 + 160] == wallpaper_with_home[145 * 240 + 160]);
    const auto pet_position = framebuffer;
    assert(pet_position != wallpaper_with_home);
    pet.show({28, 40, 80, 80}, nullptr, -1, idle.data());
    lv_refr_now(display);
    assert(framebuffer != pet_position);
    for (int y = 145; y < 225; ++y) for (int x = 160; x < 240; ++x)
      assert(framebuffer[y * 240 + x] == wallpaper_with_home[y * 240 + x]);
    // An existing SD animation frame carries a black matte. The background
    // remains visible through it, and the changed pixels repaint at the same position.
    std::array<uint16_t, smart_speaker::product::kPetSpritePixels> frame{};
    frame[60 * 120 + 60] = 0xf800;
    flushed_pixels = 0;
    pet.show({28, 40, 80, 80}, frame.data(), 0, idle.data());
    lv_refr_now(display);
    std::cout << "Pet frame damage: " << flushed_pixels << " pixels\n";
    assert(flushed_pixels <= 80 * 80 && "Pet frame must not redraw transparent padding");
    assert(framebuffer[80 * 240 + 68] == 0xf800);
    assert(framebuffer[40 * 240 + 28] == wallpaper_with_home[40 * 240 + 28]);
    for (int side : {96, 73, 80}) {
      pet.show({28, 40, side, side}, frame.data(), 0, idle.data());
      lv_refr_now(display);
      const auto incremental_pet = framebuffer;
      lv_obj_invalidate(lv_screen_active());
      lv_refr_now(display);
      assert(framebuffer == incremental_pet);
      assert(framebuffer[(40 + side / 2 + side % 2) * 240 + 28 + side / 2 + side % 2] == 0xf800);
    }
    pet.hide();
    lv_refr_now(display);
    assert(framebuffer == wallpaper_with_home);
    AppLauncher launcher;
    launcher.create(lv_screen_active(), fonts[5], fonts[4]);
    for (unsigned i = 0; i < 7; ++i) {
      launcher.show(i);
      save(output / ("app-" + std::to_string(i + 1) + ".ppm"));
    }
    // Exercise the production conversation surface over a real app page and
    // verify that dismissal restores every underlying pixel.
    lv_obj_set_style_text_font(lv_screen_active(), fonts[2], 0);
    lv_refr_now(display);
    const auto source_app = framebuffer;
    for (const char *caption : {"小智连接中", "小智正在听", "小智思考中", "这是一次来自大肥鱼的回答，结束后会回到原来的页面。"}) {
      pet.show({80, 48, 80, 80}, nullptr, -1, idle.data());
      pet.set_conversation(true, caption);
      lv_refr_now(display);
      assert(framebuffer != source_app);
    }
    save(output / "assistant-response.ppm");
    pet.hide();
    pet.set_conversation(false, "");
    lv_refr_now(display);
    assert(framebuffer == source_app);
  }
  // Delete the screen before releasing its fonts, as on the production owner.
  lv_obj_clean(lv_screen_active());
  for (auto *font : fonts) lv_tiny_ttf_destroy(font);
  lv_deinit();
  std::cout << "Home/boot states, deadlines, clock wrap, 240x240 bounds, glyph coverage and view lifetime PASS\n";
}
