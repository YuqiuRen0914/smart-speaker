#include "diagnostics/function_probe.h"
#include "pet_render_core.h"
#include "assets.hpp"
#include "color.hpp"
#include "path_data.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

#ifdef ESP_PLATFORM
#include <esp_timer.h>
#include <esp_attr.h>
#include "diagnostics/cpu_monitor.h"
#include "diagnostics/current_task_runtime.h"
#include "sdkconfig.h"
#endif

#if defined(ESP_PLATFORM) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#define PET_RASTER_DATA EXT_RAM_BSS_ATTR
#else
#define PET_RASTER_DATA
#endif

namespace {

bool g_native_rgb565=false;
uint16_t wire_rgb565(uint32_t rgb,float opacity=1.0F){
  const auto wire=lunaredu::pet::render::wire_rgb565(rgb,opacity);
  return g_native_rgb565?uint16_t((wire<<8)|(wire>>8)):wire;
}

constexpr int kBodyPointCount = 96;
constexpr int kEyePointCount = 48;
constexpr int kRasterEdgeCapacity =
    std::max({kBodyPointCount, kEyePointCount,
              lunaredu::pet::render::PathPointCapacity});
constexpr int kRasterSamples = 2;
constexpr float kPi = 3.14159265358979323846F;
// CharacterMotionLab uses a 259-unit SVG viewBox and applies poseScale to the
// whole SVG. Keep that coordinate system on the LCD instead of fitting a new
// 65%-diameter circle to each display.
constexpr float kSourceView = 259.0F;
constexpr float kSourceRadius = 114.2705F;
constexpr float kViewScale = kSourceView / 229.0F;
constexpr float kShapeZoom[] = {
    1.0F, 0.92F, 0.96F, 1.0F, 1.0F, 0.84F, 1.0F, 1.0F, 1.0F,
    0.94F, 1.0F, 1.0F, 0.94F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F};

struct Point {
  float x = 0;
  float y = 0;
};

struct Transform {
  float center_x = 120;
  float center_y = 120;
  float xx = 1;
  float xy = 0;
  float yx = 0;
  float yy = 1;
};

struct RasterEdge {
  int first_row = 0;
  int end_row = 0;
  float first_x = 0;
  float x_step = 0;
};

uint16_t *g_frame = nullptr;
int g_width = 0;
int g_height = 0;
int g_stride = 0;
int g_body_diameter = 0;
bool g_antialias = false;
float g_content_scale = 1.0F;
PetRegion g_visible_bounds{};
struct PixelHistory {
  uint16_t *pixels = nullptr;
  PetRegion bounds{};
};
PixelHistory g_pixel_history[3]{};
unsigned g_next_pixel_history = 0;
// Task-owned CPU scratch; never accessed by an ISR or used as DMA storage.
// Keep internal SRAM available for audio DMA and cache-safe task stacks.
PET_RASTER_DATA RasterEdge g_raster_edges[kRasterEdgeCapacity];
PET_RASTER_DATA int g_active_edges[kRasterEdgeCapacity];
PET_RASTER_DATA float g_crossings[kRasterEdgeCapacity];
std::vector<float> g_row_coverage;
std::vector<float> g_row_full_coverage;
// AA spans change coverage only at boundary events. Keep this small index in
// PSRAM, then fill opaque interior runs without per-pixel floating point work.
// Wider surfaces retain the original dense path, with identical coverage.
constexpr int kCoverageEventWidth = 512;
PET_RASTER_DATA uint32_t g_coverage_events[kCoverageEventWidth / 32];
PET_RASTER_DATA Point g_body_points[kBodyPointCount];
PET_RASTER_DATA Point g_body_local[kBodyPointCount];
struct BodyContourKey {int to=0,from=0;float mix=0,roundness=0;bool valid=false;};
PET_RASTER_DATA BodyContourKey g_body_contour_key;
PET_RASTER_DATA Point g_eye_polys[2][kEyePointCount];
PET_RASTER_DATA Point g_eye_centers[2];
PET_RASTER_DATA float g_eye_weights[25];
bool g_eye_weights_valid=false;
PET_RASTER_DATA Point g_path_points[lunaredu::pet::render::PathPointCapacity];
uint16_t g_body_wire_color = 0;
PET_RASTER_DATA Point g_ink[64];
int g_ink_count = 0;
int g_effect_state = -1;
void fill_polygon(const Point *points, int count, uint16_t color,
                  bool clip_to_body);
PET_RASTER_DATA PetRenderProfile g_render_profile{};
PetFrame g_touch_frame{};
bool g_touch_frame_valid = false;

uint64_t profile_now_us() {
#ifdef ESP_PLATFORM
  return static_cast<uint64_t>(esp_timer_get_time());
#else
  return 0;
#endif
}

Point apply(const Transform &transform, float x, float y) {
  return {transform.center_x + transform.xx * x + transform.xy * y,
          transform.center_y + transform.yx * x + transform.yy * y};
}

uint16_t blend_wire_rgb565(uint16_t background, uint16_t foreground,
                           float coverage) {
  const int alpha = std::clamp(static_cast<int>(coverage * 256.0F + 0.5F),
                               0, 256);
  if (alpha == 0) return background;
  if (alpha == 256) return foreground;
  const uint16_t from = g_native_rgb565?background:static_cast<uint16_t>((background >> 8) | (background << 8));
  const uint16_t to = g_native_rgb565?foreground:static_cast<uint16_t>((foreground >> 8) | (foreground << 8));
  const int inverse = 256 - alpha;
  const uint16_t blended = static_cast<uint16_t>(
      (((((from >> 11) & 31) * inverse + ((to >> 11) & 31) * alpha + 128) >> 8)
       << 11) |
      (((((from >> 5) & 63) * inverse + ((to >> 5) & 63) * alpha + 128) >> 8)
       << 5) |
      (((from & 31) * inverse + (to & 31) * alpha + 128) >> 8));
  return g_native_rgb565?blended:static_cast<uint16_t>((blended >> 8) | (blended << 8));
}

int g_coverage_width = 0;
int g_coverage_left = 0;
int g_coverage_right = 0;

void begin_coverage_row() {
  if (g_row_coverage.size() < static_cast<size_t>(g_width)) {
    g_row_coverage.resize(g_width);
    g_row_full_coverage.resize(g_width + 1);
  }
  if (g_coverage_width != g_width) {
    std::fill(g_row_coverage.begin(), g_row_coverage.begin() + g_width, 0.0F);
    std::fill(g_row_full_coverage.begin(), g_row_full_coverage.begin() + g_width + 1, 0.0F);
    g_coverage_width = g_width;
  } else if (g_width <= kCoverageEventWidth) {
    // Only boundary events write these arrays. Clear their previous entries,
    // not every interior pixel of every row in the PSRAM coverage scratch.
    for (unsigned word = 0; word < std::size(g_coverage_events); ++word) {
      auto bits = g_coverage_events[word];
      while (bits) {
        const unsigned x = word * 32 + __builtin_ctz(bits);
        g_row_coverage[x] = g_row_full_coverage[x] = 0.0F;
        bits &= bits - 1;
      }
    }
  } else {
    // Wider canvases use the unchanged dense path, including its terminator.
    const int first = std::clamp(g_coverage_left, 0, g_width);
    const int last = std::clamp(g_coverage_right, first, g_width);
    std::fill(g_row_coverage.begin() + first, g_row_coverage.begin() + last, 0.0F);
    std::fill(g_row_full_coverage.begin() + first,
              g_row_full_coverage.begin() + last + 1, 0.0F);
  }
  std::fill(std::begin(g_coverage_events), std::end(g_coverage_events), 0U);
  g_coverage_left = g_width;
  g_coverage_right = 0;
}

void mark_coverage_event(int x) {
  if (g_width <= kCoverageEventWidth && x >= 0 && x < kCoverageEventWidth)
    g_coverage_events[x/32] |= uint32_t{1} << (x%32);
}

// Exact ceil-to-integer inside an already bounded raster coordinate range.
// S3 otherwise calls general ceilf and then truncates its float result for
// every polygon edge/subrow. No rounding approximation or reduced AA samples.
int ceil_raster(float value,int minimum,int maximum) {
  if(!(value>float(minimum)))return minimum;
  if(value>=float(maximum))return maximum;
  const int truncated=static_cast<int>(value);
  return truncated+int(float(truncated)<value);
}

void add_coverage_span(float from, float to, float weight) {
  from = std::clamp(from, 0.0F, static_cast<float>(g_width));
  to = std::clamp(to, 0.0F, static_cast<float>(g_width));
  if (to <= from) return;
  // from/to are nonnegative and clipped to the allocated row. For a
  // positive right boundary, ceil(to)-1 is trunc(to) minus exact-integer.
  const int first = static_cast<int>(from);
  const int right = static_cast<int>(to);
  const int last = right-int(float(right)==to);
  g_coverage_left = std::min(g_coverage_left, first);
  g_coverage_right = std::max(g_coverage_right, last + 1);
  mark_coverage_event(first);
  mark_coverage_event(last);
  if (first == last) {
    g_row_coverage[first] += (to - from) * weight;
    return;
  }
  g_row_coverage[first] += (first + 1 - from) * weight;
  g_row_coverage[last] += (to - last) * weight;
  if (first + 1 < last) {
    mark_coverage_event(first + 1);
    g_row_full_coverage[first + 1] += weight;
    g_row_full_coverage[last] -= weight;
  }
}

float next_coverage(int x, float &full_coverage) {
  full_coverage += g_row_full_coverage[x];
  return std::clamp(full_coverage + g_row_coverage[x], 0.0F, 1.0F);
}

void paint_coverage_row(int y, uint16_t color, bool clip_to_body = false) {
  float full_coverage = 0.0F;
  uint16_t *const row = g_frame + y * g_stride;
  // Coverage changes only at boundary events, including body-clipped eyes.
  // Clipping still checks each destination pixel; constant interior runs do
  // not need per-pixel PSRAM coverage reads and floating point prefix sums.
  if (g_width <= kCoverageEventWidth) {
    int x = g_coverage_left;
    while (x < g_coverage_right) {
      // Evaluate actual events in the same order as the dense prefix sum.
      const float coverage = next_coverage(x, full_coverage);
      if (coverage > 0 && (!clip_to_body || row[x] == g_body_wire_color))
        row[x] = coverage >= 0.999F ? color : blend_wire_rgb565(row[x], color, coverage);
      const int first = ++x;
      int next = g_coverage_right;
      for (int word = x/32; word < (g_coverage_right+31)/32; ++word) {
        auto bits = g_coverage_events[word];
        if (word == x/32) bits &= ~uint32_t{0} << (x%32);
        if (bits) { next = std::min(next, word*32+__builtin_ctz(bits)); break; }
      }
      const float interior = std::clamp(full_coverage, 0.0F, 1.0F);
      if (interior >= 0.999F && !clip_to_body) {
        std::fill(row+first, row+next, color);
      } else if (interior > 0) {
        for (int pixel=first; pixel<next; ++pixel)
          if (!clip_to_body || row[pixel] == g_body_wire_color)
            row[pixel] = interior >= 0.999F ? color : blend_wire_rgb565(row[pixel],color,interior);
      }
      x = next;
    }
    return;
  }
  for (int x = g_coverage_left; x < g_coverage_right; ++x) {
    const float coverage = next_coverage(x, full_coverage);
    if (coverage <= 0.0F) continue;
    if (clip_to_body && row[x] != g_body_wire_color) continue;
    row[x] = coverage >= 0.999F
                 ? color : blend_wire_rgb565(row[x], color, coverage);
  }
}

void include_bounds(const Point *points, int count) {
  if (points == nullptr || count < 1) return;
  float left = points[0].x;
  float right = points[0].x;
  float top = points[0].y;
  float bottom = points[0].y;
  for (int index = 1; index < count; ++index) {
    left = std::min(left, points[index].x);
    right = std::max(right, points[index].x);
    top = std::min(top, points[index].y);
    bottom = std::max(bottom, points[index].y);
  }
  const int x0 = std::clamp(static_cast<int>(std::floor(left)), 0, g_width);
  const int y0 = std::clamp(static_cast<int>(std::floor(top)), 0, g_height);
  const int x1 = std::clamp(static_cast<int>(std::ceil(right)) + 1, 0, g_width);
  const int y1 = std::clamp(static_cast<int>(std::ceil(bottom)) + 1, 0, g_height);
  if (x0 >= x1 || y0 >= y1) return;
  if (g_visible_bounds.width <= 0 || g_visible_bounds.height <= 0) {
    g_visible_bounds = {x0, y0, x1 - x0, y1 - y0};
    return;
  }
  const int merged_x = std::min(g_visible_bounds.x, x0);
  const int merged_y = std::min(g_visible_bounds.y, y0);
  const int merged_right =
      std::max(g_visible_bounds.x + g_visible_bounds.width, x1);
  const int merged_bottom =
      std::max(g_visible_bounds.y + g_visible_bounds.height, y1);
  g_visible_bounds = {merged_x, merged_y, merged_right - merged_x,
                      merged_bottom - merged_y};
}

void draw_effect_disc(float center_x, float center_y, float radius,
                      uint16_t color) {
  if (radius <= 0.0F) return;
  const int left = std::clamp(static_cast<int>(std::floor(center_x - radius)),
                              0, g_width);
  const int right = std::clamp(static_cast<int>(std::ceil(center_x + radius)) + 1,
                               0, g_width);
  const int top = std::clamp(static_cast<int>(std::floor(center_y - radius)),
                             0, g_height);
  const int bottom = std::clamp(static_cast<int>(std::ceil(center_y + radius)) + 1,
                                0, g_height);
  if (left >= right || top >= bottom) return;
  const Point bounds[] = {{static_cast<float>(left), static_cast<float>(top)},
                          {static_cast<float>(right), static_cast<float>(bottom)}};
  include_bounds(bounds, 2);
  const float radius_squared = radius * radius;
  if (!g_antialias) {
    for (int y = top; y < bottom; ++y) {
      const float dy = y + 0.5F - center_y;
      if (dy * dy >= radius_squared) continue;
      const float half_width = std::sqrt(radius_squared - dy * dy);
      const int x0 = std::clamp(static_cast<int>(std::ceil(
          center_x - half_width - 0.5F)), left, right);
      const int x1 = std::clamp(static_cast<int>(std::ceil(
          center_x + half_width - 0.5F)), x0, right);
      std::fill(g_frame + y * g_stride + x0,
                g_frame + y * g_stride + x1, color);
    }
    return;
  }
  for (int y = top; y < bottom; ++y) {
    begin_coverage_row();
    for (int sample = 0; sample < kRasterSamples; ++sample) {
      const float dy = y + (sample + 0.5F) / kRasterSamples - center_y;
      if (dy * dy >= radius_squared) continue;
      const float half_width = std::sqrt(radius_squared - dy * dy);
      add_coverage_span(center_x - half_width, center_x + half_width,
                        1.0F / kRasterSamples);
    }
    paint_coverage_row(y, color);
  }
}

void draw_effect_ring(float center_x, float center_y, float radius,
                      float thickness, uint16_t color) {
  if (radius <= 0.0F || thickness <= 0.0F) return;
  const float inner = std::max(0.0F, radius - thickness * 0.5F);
  const float outer = radius + thickness * 0.5F;
  const int left = std::clamp(static_cast<int>(std::floor(center_x - outer)),
                              0, g_width);
  const int right = std::clamp(static_cast<int>(std::ceil(center_x + outer)) + 1,
                               0, g_width);
  const int top = std::clamp(static_cast<int>(std::floor(center_y - outer)),
                             0, g_height);
  const int bottom = std::clamp(static_cast<int>(std::ceil(center_y + outer)) + 1,
                                0, g_height);
  if (left >= right || top >= bottom) return;
  const Point bounds[] = {{static_cast<float>(left), static_cast<float>(top)},
                          {static_cast<float>(right), static_cast<float>(bottom)}};
  include_bounds(bounds, 2);
  const float outer_squared = outer * outer;
  const float inner_squared = inner * inner;
  if (!g_antialias) {
    for (int y = top; y < bottom; ++y) {
      const float dy = y + 0.5F - center_y;
      if (dy * dy >= outer_squared) continue;
      const float outer_width = std::sqrt(outer_squared - dy * dy);
      const float inner_width = dy * dy < inner_squared
          ? std::sqrt(inner_squared - dy * dy) : 0.0F;
      const int x0 = std::clamp(static_cast<int>(std::ceil(
          center_x - outer_width - 0.5F)), left, right);
      const int x1 = std::clamp(static_cast<int>(std::ceil(
          center_x - inner_width - 0.5F)), x0, right);
      const int x2 = std::clamp(static_cast<int>(std::ceil(
          center_x + inner_width - 0.5F)), x1, right);
      const int x3 = std::clamp(static_cast<int>(std::ceil(
          center_x + outer_width - 0.5F)), x2, right);
      std::fill(g_frame + y * g_stride + x0,
                g_frame + y * g_stride + x1, color);
      std::fill(g_frame + y * g_stride + x2,
                g_frame + y * g_stride + x3, color);
    }
    return;
  }
  for (int y = top; y < bottom; ++y) {
    begin_coverage_row();
    for (int sample = 0; sample < kRasterSamples; ++sample) {
      const float dy = y + (sample + 0.5F) / kRasterSamples - center_y;
      if (dy * dy >= outer_squared) continue;
      const float outer_width = std::sqrt(outer_squared - dy * dy);
      if (dy * dy >= inner_squared) {
        add_coverage_span(center_x - outer_width, center_x + outer_width,
                          1.0F / kRasterSamples);
      } else {
        const float inner_width = std::sqrt(inner_squared - dy * dy);
        add_coverage_span(center_x - outer_width, center_x - inner_width,
                          1.0F / kRasterSamples);
        add_coverage_span(center_x + inner_width, center_x + outer_width,
                          1.0F / kRasterSamples);
      }
    }
    paint_coverage_row(y, color);
  }
}

void draw_effect_progress_ring(float center_x, float center_y, float radius,
                               float thickness, float progress,
                               uint16_t background, uint16_t foreground) {
  if (radius <= 0.0F || thickness <= 0.0F) return;
  const float inner = std::max(0.0F, radius - thickness * 0.5F);
  const float outer = radius + thickness * 0.5F;
  const int left = std::clamp(static_cast<int>(std::floor(center_x - outer)),
                              0, g_width);
  const int right = std::clamp(static_cast<int>(std::ceil(center_x + outer)) + 1,
                               0, g_width);
  const int top = std::clamp(static_cast<int>(std::floor(center_y - outer)),
                             0, g_height);
  const int bottom = std::clamp(static_cast<int>(std::ceil(center_y + outer)) + 1,
                                0, g_height);
  if (left >= right || top >= bottom) return;
  const Point bounds[] = {{static_cast<float>(left), static_cast<float>(top)},
                          {static_cast<float>(right), static_cast<float>(bottom)}};
  include_bounds(bounds, 2);
  const float outer_squared = outer * outer;
  const float inner_squared = inner * inner;
  const float sweep = std::clamp(progress, 0.0F, 1.0F) * 2.0F * kPi;
  const float endpoint_x = std::sin(sweep);
  const float endpoint_y = -std::cos(sweep);
  for (int y = top; y < bottom; ++y) {
    if (!g_antialias) {
      const float dy = y + 0.5F - center_y;
      if (dy * dy >= outer_squared) continue;
      const float outer_width = std::sqrt(outer_squared - dy * dy);
      const float inner_width = dy * dy < inner_squared
          ? std::sqrt(inner_squared - dy * dy) : 0.0F;
      const int x0 = std::clamp(static_cast<int>(std::ceil(
          center_x - outer_width - 0.5F)), left, right);
      const int x1 = std::clamp(static_cast<int>(std::ceil(
          center_x - inner_width - 0.5F)), x0, right);
      const int x2 = std::clamp(static_cast<int>(std::ceil(
          center_x + inner_width - 0.5F)), x1, right);
      const int x3 = std::clamp(static_cast<int>(std::ceil(
          center_x + outer_width - 0.5F)), x2, right);
      const auto paint_span = [&](int begin, int end) {
        for (int x = begin; x < end; ++x) {
          const float dx = x + 0.5F - center_x;
          const float cross = dx * endpoint_y - dy * endpoint_x;
          const bool filled = progress >= 1.0F ||
              (progress > 0.0F && (sweep <= kPi
                  ? dx >= 0.0F && cross >= 0.0F
                  : dx >= 0.0F || cross >= 0.0F));
          g_frame[y * g_stride + x] = filled ? foreground : background;
        }
      };
      paint_span(x0, x1);
      paint_span(x2, x3);
      continue;
    }
    begin_coverage_row();
    for (int sample = 0; sample < kRasterSamples; ++sample) {
      const float dy = y + (sample + 0.5F) / kRasterSamples - center_y;
      if (dy * dy >= outer_squared) continue;
      const float outer_width = std::sqrt(outer_squared - dy * dy);
      if (dy * dy >= inner_squared) {
        add_coverage_span(center_x - outer_width, center_x + outer_width,
                          1.0F / kRasterSamples);
      } else {
        const float inner_width = std::sqrt(inner_squared - dy * dy);
        add_coverage_span(center_x - outer_width, center_x - inner_width,
                          1.0F / kRasterSamples);
        add_coverage_span(center_x + inner_width, center_x + outer_width,
                          1.0F / kRasterSamples);
      }
    }
    float full_coverage = 0.0F;
    uint16_t *const row = g_frame + y * g_stride;
    for (int x = g_coverage_left; x < g_coverage_right; ++x) {
      const float coverage = next_coverage(x, full_coverage);
      if (coverage <= 0.0F) continue;
      float fill_fraction = progress >= 1.0F ? 1.0F : 0.0F;
      if (progress > 0.0F && progress < 1.0F) {
        for (int sy = 0; sy < 2; ++sy) for (int sx = 0; sx < 2; ++sx) {
          const float dx = x + (sx + 0.5F) * 0.5F - center_x;
          const float dy = y + (sy + 0.5F) * 0.5F - center_y;
          const float cross = dx * endpoint_y - dy * endpoint_x;
          fill_fraction += (sweep <= kPi
              ? dx >= 0.0F && cross >= 0.0F
              : dx >= 0.0F || cross >= 0.0F) ? 0.25F : 0.0F;
        }
      }
      const uint16_t ring_color = blend_wire_rgb565(
          background, foreground, fill_fraction);
      row[x] = blend_wire_rgb565(row[x], ring_color, coverage);
    }
  }
}

void draw_effect_segment(float ax, float ay, float bx, float by,
                         float width, uint16_t color) {
  const float radius = width * 0.5F;
  const float edge_radius = radius + (g_antialias ? 0.5F : 0.0F);
  const int left = std::clamp(static_cast<int>(std::floor(
                                  std::min(ax, bx) - edge_radius)), 0, g_width);
  const int right = std::clamp(static_cast<int>(std::ceil(
                                   std::max(ax, bx) + edge_radius)) + 1,
                               0, g_width);
  const int top = std::clamp(static_cast<int>(std::floor(
                                 std::min(ay, by) - edge_radius)), 0, g_height);
  const int bottom = std::clamp(static_cast<int>(std::ceil(
                                    std::max(ay, by) + edge_radius)) + 1,
                                0, g_height);
  if (left >= right || top >= bottom) return;
  const Point bounds[] = {{static_cast<float>(left), static_cast<float>(top)},
                          {static_cast<float>(right), static_cast<float>(bottom)}};
  include_bounds(bounds, 2);
  const float dx = bx - ax;
  const float dy = by - ay;
  const float length_squared = dx * dx + dy * dy;
  const float inverse_length_squared = length_squared > 0.0001F
      ? 1.0F / length_squared : 0.0F;
  const float inner_radius = std::max(0.0F, radius - 0.5F);
  const float inner_squared = inner_radius * inner_radius;
  const float outer_squared = edge_radius * edge_radius;
  for (int y = top; y < bottom; ++y) {
    float projection = ((left + 0.5F - ax) * dx +
                        (y + 0.5F - ay) * dy) * inverse_length_squared;
    for (int x = left; x < right; ++x) {
      const float t = std::clamp(projection, 0.0F, 1.0F);
      projection += dx * inverse_length_squared;
      const float px = x + 0.5F - ax - t * dx;
      const float py = y + 0.5F - ay - t * dy;
      const float distance_squared = px * px + py * py;
      if (!g_antialias) {
        if (distance_squared <= outer_squared)
          g_frame[y * g_stride + x] = color;
        continue;
      }
      if (distance_squared >= outer_squared) continue;
      uint16_t &pixel = g_frame[y * g_stride + x];
      pixel = distance_squared <= inner_squared
                  ? color
                  : blend_wire_rgb565(pixel, color,
                      edge_radius - std::sqrt(distance_squared));
    }
  }
}

constexpr int kRibbonMaxSegments=35;
struct RibbonGeometry {
  Point points[4][kRibbonMaxSegments+1]{};
  bool front[4][kRibbonMaxSegments]{};
  float spin=0,scale=0,center_x=0,center_y=0;
  int segments=0;
  bool valid=false;
};
PET_RASTER_DATA RibbonGeometry g_ribbon_geometry;

void draw_state_ribbons(const PetFrame &g, bool front) {
  if (g.ribbon <= 0.01F) return;
  // fx.js projects orbiting ribbons through a tilted plane and splits each
  // trail at the front/back depth boundary. Keep that projection in pixels.
  constexpr uint32_t kColors[] = {
      0xf9705cU, 0x5b95f0U, 0x3fbe86U, 0xf5b13fU,
      0x9a72eeU, 0x35c3bdU};
  const float scale = std::min(g_width, g_height) / kSourceView *
      kShapeZoom[std::clamp(g.shape_to, 0, lunaredu::pet::render::ShapeCount - 1)] *
      kViewScale * g_content_scale;
  // Keep chord error below half a pixel across product display sizes. The
  // 240 px Xiaoxing surface uses 16 segments; larger pets gain more detail.
  const float outer_radius = (116.0F + 3.0F * 11.0F) * scale;
  const int ribbon_segments = std::clamp(
      static_cast<int>(std::ceil(2.8F * std::sqrt(outer_radius * 0.25F))),
      16, kRibbonMaxSegments);
  const float center_x = g_width * 0.5F;
  const float center_y = g_height * 0.5F;
  auto &geometry=g_ribbon_geometry;
  if(!geometry.valid||geometry.spin!=g.fx_spin||geometry.scale!=scale||
      geometry.center_x!=center_x||geometry.center_y!=center_y||geometry.segments!=ribbon_segments){
    // Back/front layers share identical geometry. Adjacent trail segments also
    // share a projected endpoint; evaluate its strict-FP trig expression once.
    for(int belt=0;belt<4;++belt){
      const float tilt=0.24F+0.075F*(belt%3),roll=-0.55F+belt*kPi/4.0F;
      const float tilt_sine=std::sin(tilt),roll_sine=std::sin(roll),roll_cosine=std::cos(roll);
      const float radius=(116.0F+belt*11.0F)*scale;
      const float head=g.fx_spin*0.85F+belt*2.0F*kPi/4.0F;
      for(int point=0;point<=ribbon_segments;++point){
        const float angle=head-2.8F+point*2.8F/ribbon_segments;
        const float x=radius*std::sin(angle),y=-radius*std::cos(angle)*tilt_sine;
        geometry.points[belt][point]={center_x+x*roll_cosine-y*roll_sine,center_y+x*roll_sine+y*roll_cosine};
      }
      for(int segment=0;segment<ribbon_segments;++segment){
        const float from=head-2.8F+segment*2.8F/ribbon_segments;
        const float to=head-2.8F+(segment+1)*2.8F/ribbon_segments;
        geometry.front[belt][segment]=std::cos((from+to)*0.5F)>=0.0F;
      }
    }
    geometry.spin=g.fx_spin;geometry.scale=scale;geometry.center_x=center_x;geometry.center_y=center_y;
    geometry.segments=ribbon_segments;geometry.valid=true;
  }
  for(int belt=0;belt<4;++belt)for(int segment=0;segment<ribbon_segments;++segment){
    if(geometry.front[belt][segment]!=front)continue;
    const auto a=geometry.points[belt][segment],b=geometry.points[belt][segment+1];
    const float tail=static_cast<float>(segment)/ribbon_segments;
    draw_effect_segment(a.x,a.y,b.x,b.y,(4.0F+5.0F*tail)*scale,
        wire_rgb565(kColors[belt%std::size(kColors)],g.ribbon*(0.3F+0.7F*tail)));
  }
}

void draw_state_effects(const PetFrame &g, uint32_t ink) {
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::Effects);
  const float scale = std::min(g_width, g_height) / kSourceView *
      kShapeZoom[std::clamp(g.shape_to, 0, lunaredu::pet::render::ShapeCount - 1)] *
      kViewScale * g_content_scale;
  const float center_x = g_width * 0.5F;
  const float center_y = g_height * 0.5F;
  const float alpha = std::clamp(g.overlay, 0.0F, 1.0F);
  const auto fraction = [](float value) { return value - std::floor(value); };
  const auto clamp01 = [](float value) {
    return std::clamp(value, 0.0F, 1.0F);
  };
  const auto ease = [&clamp01](float value) {
    const float inverse = 1.0F - clamp01(value);
    return 1.0F - inverse * inverse * inverse;
  };
  const auto back = [&clamp01](float value) {
    const float x = clamp01(value) - 1.0F;
    return 1.0F + 2.70158F * x * x * x + 1.70158F * x * x;
  };
  const auto color = [ink](float opacity) { return wire_rgb565(ink, opacity); };
  switch (g.state) {
  case lunaredu::pet::render::thinking: {
    const float phase = fraction(g.overlay_time / 1.4F + 0.119F);
    for (int side = 0; side < 2; ++side) {
      float distance = std::abs(phase - (side == 0 ? 0.0F : 2.0F / 3.0F));
      distance = std::min(distance, 1.0F - distance);
      const float pulse = std::exp(-distance * distance / (2.0F * 0.15F * 0.15F));
      draw_effect_disc(center_x + (side == 0 ? -62.0F : 62.0F) *
                           back(alpha) * scale,
                       center_y - pulse * 9.0F * scale,
                       22.0F * (0.84F + 0.22F * pulse) *
                           ease(alpha) * scale,
                       color(ease(alpha) * (0.5F + 0.5F * pulse)));
    }
    break;
  }
  case lunaredu::pet::render::orbit:
    for (int index = 0; index < 5; ++index) {
      const float theta = g.time * 1.7F + index * 2.0F * kPi / 5.0F;
      const float depth = std::cos(theta);
      draw_effect_disc(center_x + 52.0F * back(alpha) *
                           std::sin(theta) * scale,
                       center_y - 52.0F * back(alpha) *
                           0.42F * depth * scale,
                       12.0F * (0.5F + 0.5F * std::max(depth, 0.0F)) *
                           ease(alpha) * scale,
                       color(std::clamp((depth + 0.4F) / 0.6F,
                                        0.18F, 1.0F) * ease(alpha)));
    }
    break;
  case lunaredu::pet::render::radar:
    for (int index = 0; index < 3; ++index) {
      const float progress = fraction(g.time / 1.3F + index / 3.0F);
      draw_effect_ring(center_x, center_y,
                       (19.0F + 85.0F * progress) * scale,
                       3.4F * (1.0F - progress * 0.55F) * scale,
                       color(ease(alpha) * (1.0F - progress) * 0.9F));
    }
    break;
  case lunaredu::pet::render::progress: {
    const float radius = 62.0F * back(alpha) * scale;
    const float progress = g.progress_fraction >= 0.0F
                               ? clamp01(g.progress_fraction)
                               : clamp01(g.overlay_time / 2.5F / 0.85F);
    draw_effect_progress_ring(center_x, center_y, radius, 5.0F * scale,
                              progress, color(ease(alpha) * 0.16F),
                              color(ease(alpha)));
    break;
  }
  case lunaredu::pet::render::spawning:
    for (int index = 0; index < 5; ++index) {
      const float progress = clamp01(
          (g.overlay_time / 2.0F - index * 0.09F) / 0.62F);
      if (progress >= 1.0F) continue;
      const float arrived = ease(progress);
      const float angle = index * 2.4F + progress * 2.2F;
      const float radius = 96.0F * (1.0F - arrived);
      draw_effect_disc(center_x + radius * std::cos(angle) * scale,
                       center_y + radius * std::sin(angle) * 0.8F * scale,
                       9.0F * (0.5F + 0.5F * arrived) * ease(alpha) * scale,
                       color(ease(alpha) * clamp01(progress * 5.0F) *
                             (1.0F - arrived * 0.25F)));
    }
    break;
  case lunaredu::pet::render::dictating:
    for (int side = -2; side <= 2; ++side) {
      if (side == 0) continue;
      const float amount = clamp01(
          (alpha - std::abs(side) * 0.1F) /
          (1.0F - std::abs(side) * 0.1F));
      if (amount <= 0.004F) continue;
      const float wave = 0.42F +
          0.29F * std::sin(g.time * 2.1F) * std::sin(g.time * 3.4F) +
          0.29F * std::sin(g.time * 1.3F + 1.7F);
      const float signal = wave * (0.55F + 0.45F * std::sin(
          g.time * 12.0F - std::abs(side) * 1.05F));
      draw_effect_disc(center_x + side * 44.0F * back(amount) * scale,
                       center_y - 6.0F * clamp01(signal) * amount * scale,
                       (7.0F + 9.0F * std::clamp(signal, 0.08F, 1.0F)) *
                           ease(amount) * scale,
                       color(amount));
    }
    break;
  case lunaredu::pet::render::sending: {
    const float phase = fraction(g.state_time / 1.5F);
    for (int index = 0; index < 2; ++index) {
      const float progress = clamp01(
          (phase - (index == 0 ? 0.18F : 0.26F)) / 0.55F);
      if (progress <= 0.0F || progress >= 1.0F) continue;
      const float moved = progress * progress * (0.4F + 0.6F * progress);
      draw_effect_disc(center_x + 0.74F * 108.0F * moved * scale,
                       center_y - 0.62F * 108.0F * moved * scale,
                       (index == 0 ? 10.0F : 5.0F) *
                           (1.0F - moved * (index == 0 ? 0.55F : 0.6F)) *
                           ease(alpha) * scale,
                       color(ease(alpha) * (index == 0 ? 1.0F : 0.3F) *
                             (index == 0 ? 1.0F - moved * moved
                                         : 1.0F - moved)));
    }
    const float ripple = clamp01((phase - 0.18F) / 0.3F);
    if (ripple > 0.0F && ripple < 1.0F) {
      draw_effect_ring(center_x, center_y,
                       (20.0F + 34.0F * ease(ripple)) * scale,
                       2.8F * (1.0F - ripple) * scale,
                       color(ease(alpha) * (1.0F - ripple) * 0.8F));
    }
    break;
  }
  case lunaredu::pet::render::receiving: {
    const float phase = fraction(g.state_time / 1.7F);
    const float progress = clamp01(phase / 0.6F);
    const float arrived = ease(progress);
    if (progress < 1.0F) {
      const float direction = g.fx_receive_direction;
      const float bend = 18.0F * std::sin(progress * kPi) *
                         (1.0F - arrived * 0.7F);
      const float distance = 108.0F * (1.0F - arrived);
      draw_effect_disc(center_x +
                           (std::cos(direction) * distance -
                            std::sin(direction) * bend) * scale,
                       center_y +
                           (std::sin(direction) * distance +
                            std::cos(direction) * bend) * scale,
                       (3.5F + 6.5F * arrived) * scale,
                       color(ease(alpha) * clamp01(progress * 3.5F) *
                             (0.3F + 0.7F * arrived)));
    }
    const float ripple = clamp01((phase - 0.58F) / 0.32F);
    if (ripple > 0.0F && ripple < 1.0F) {
      draw_effect_ring(center_x, center_y,
                       (20.0F + 26.0F * ease(ripple)) * scale,
                       2.8F * (1.0F - ripple) * scale,
                       color(ease(alpha) * (1.0F - ripple) * 0.8F));
    }
    break;
  }
  case lunaredu::pet::render::uploading:
    for (int index = 0; index < 2; ++index) {
      const float progress = clamp01(
          (g.state_time - (0.2F + index * 1.3F)) / 0.9F);
      if (progress <= 0.0F) continue;
      const float arrived = ease(progress);
      const float angle = g.time * 1.1F + index * kPi;
      const float start_x = -120.0F + index * 30.0F;
      const float end_x = 42.0F * std::sin(angle);
      const float end_y = 21.0F * std::cos(angle) +
                          2.0F * std::sin(g.time * 3.0F + index);
      draw_effect_disc(center_x +
                           (start_x * (1.0F - arrived) +
                            end_x * arrived) * scale,
                       center_y +
                           (95.0F * (1.0F - arrived) +
                            end_y * arrived) * scale,
                       (7.0F + 3.0F * arrived) * ease(alpha) * scale,
                       color(ease(alpha) * clamp01(progress * 4.0F)));
    }
    break;
  case lunaredu::pet::render::writing: {
    const float phase = fraction(g.state_time / 2.5F);
    const bool writing = phase < 0.68F;
    const float progress = writing ? phase / 0.68F :
        (phase - 0.68F) / 0.32F;
    const float moved = writing ? progress * progress *
        (3.0F - 2.0F * progress) :
        (progress < 0.5F ? 4.0F * progress * progress * progress :
         1.0F - std::pow(-2.0F * progress + 2.0F, 3.0F) * 0.5F);
    const float pen_x = writing ? -54.0F + 118.0F * moved :
        64.0F - 118.0F * moved;
    const float pen_y = writing ? 26.0F :
        26.0F - 20.0F * std::sin(moved * kPi);
    const float wobble = writing ? std::sin(progress * 24.0F) * 3.2F *
        clamp01(progress / 0.08F) * clamp01((1.0F - progress) / 0.08F) : 0.0F;
    const float tilt = (writing ? 17.0F :
        17.0F - 2.0F * std::sin(moved * kPi)) +
        std::sin(g.state_time * 0.6F);
    const float angle = tilt * alpha * kPi / 180.0F;
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    const float lean = (tilt - 90.0F) * kPi / 180.0F;
    const float origin_x = center_x +
        (pen_x + std::cos(lean) * 68.0F) * alpha * scale;
    const float origin_y = center_y +
        (pen_y + wobble * 0.15F + std::sin(lean) * 68.0F) *
            alpha * scale;
    const float size = ease(alpha) * scale;
    Point capsule[26];
    for (int point = 0; point <= 12; ++point) {
      const float a = kPi + point * kPi / 12.0F;
      const float x = 15.0F * std::cos(a) * size;
      const float y = (-29.0F + 15.0F * std::sin(a)) * size;
      capsule[point] = {origin_x + x * cosine - y * sine,
                        origin_y + x * sine + y * cosine};
    }
    for (int point = 0; point <= 12; ++point) {
      const float a = point * kPi / 12.0F;
      const float x = 15.0F * std::cos(a) * size;
      const float y = (29.0F + 15.0F * std::sin(a)) * size;
      capsule[13 + point] = {origin_x + x * cosine - y * sine,
                             origin_y + x * sine + y * cosine};
    }
    fill_polygon(capsule, 26,
                 color(clamp01(alpha * 1.6F - 0.3F)), false);

    if (alpha > 0.6F && writing) {
      const Point position = {pen_x, pen_y + wobble + 19.0F};
      if (g_ink_count == 0 || std::hypot(
          position.x - g_ink[g_ink_count - 1].x,
          position.y - g_ink[g_ink_count - 1].y) > 2.4F) {
        if (g_ink_count == 64) {
          std::move(g_ink + 1, g_ink + 64, g_ink);
          g_ink_count = 63;
        }
        g_ink[g_ink_count++] = position;
      } else {
        g_ink[g_ink_count - 1] = position;
      }
    } else if (g_ink_count > 0) {
      const int drop = std::min(2, g_ink_count);
      std::move(g_ink + drop, g_ink + g_ink_count, g_ink);
      g_ink_count -= drop;
    }
    for (int point = 1; point < g_ink_count; ++point) {
      draw_effect_segment(center_x + g_ink[point - 1].x * scale,
                          center_y + g_ink[point - 1].y * scale,
                          center_x + g_ink[point].x * scale,
                          center_y + g_ink[point].y * scale,
                          6.0F * scale,
                          color(clamp01(alpha * 1.2F)));
    }
    break;
  }
  case lunaredu::pet::render::alerting: {
    // fx.js paints taper(30, 17, 96, R) as a single capped glyph.
    const float appear = ease(clamp01(alpha * 1.1F));
    const float lift = -26.0F - (1.0F - appear) * 70.0F;
    const float size = clamp01(alpha * 1.2F);
    const float shake = std::sin(g.state_time * 42.0F) * 2.2F *
        std::exp(-fraction(g.state_time / 2.2F) * 2.2F * 5.5F) *
        kPi / 180.0F;
    const float sine = std::sin(shake);
    const float cosine = std::cos(shake);
    const float pivot_y = (lift - 74.0F) * scale;
    Point glyph[26];
    for (int point = 0; point <= 12; ++point) {
      const float angle = kPi + point * kPi / 12.0F;
      const float x = 15.0F * std::cos(angle) * size * scale;
      const float y = (-33.0F + 15.0F * std::sin(angle)) * size *
          scale + lift * scale;
      const float dy = y - pivot_y;
      glyph[point] = {center_x + x * cosine - dy * sine,
                      center_y + pivot_y + x * sine + dy * cosine};
    }
    for (int point = 0; point <= 12; ++point) {
      const float angle = point * kPi / 12.0F;
      const float x = 8.5F * std::cos(angle) * size * scale;
      const float y = (39.5F + 8.5F * std::sin(angle)) * size *
          scale + lift * scale;
      const float dy = y - pivot_y;
      glyph[13 + point] = {center_x + x * cosine - dy * sine,
                           center_y + pivot_y + x * sine + dy * cosine};
    }
    fill_polygon(glyph, 26,
                 color(clamp01(alpha * 1.5F - 0.2F)), false);
    break;
  }
  case lunaredu::pet::render::powering_down: {
    const float pulse = 0.5F + 0.5F * std::sin(g.time * 1.6F);
    draw_effect_disc(center_x, center_y, (26.0F + 7.0F * pulse) * scale,
                     color(ease(alpha) * (0.06F + 0.1F * pulse)));
    if (alpha < 0.995F) {
      draw_effect_ring(center_x, center_y,
                       (104.0F - 88.0F * ease(alpha)) * scale,
                       2.4F * scale, color((1.0F - ease(alpha)) * 0.5F));
    }
    break;
  }
  case lunaredu::pet::render::humming:
    for (int index = 0; index < 2; ++index) {
      const float angle = g.fx_spin * 0.85F + index * kPi;
      const float depth = std::cos(angle);
      const float size = 0.55F + 0.45F * clamp01((depth + 1.0F) * 0.5F);
      draw_effect_disc(center_x + 132.0F * std::sin(angle) * scale,
                       center_y - 132.0F * 0.38F * depth * scale -
                           8.0F * scale,
                       7.5F * size * scale,
                       color(0.3F + 0.7F * size));
    }
    break;
  case lunaredu::pet::render::notifying:
    if (g.notify > 0.01F) {
      // eyes.js places the badge at restRing[round(7/8 * ring.length)].
      const Point anchor = g_body_points[kBodyPointCount * 7 / 8];
      draw_effect_disc(anchor.x, anchor.y, 20.0F * g.notify * scale,
                       wire_rgb565(0x1d9bf0U));
    }
    break;
  default: break;
  }
}

void fill_polygon(const Point *points, int count, uint16_t color,
                  bool clip_to_body = false) {
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::Polygon);
  if (g_frame == nullptr || points == nullptr || count < 3) return;
  include_bounds(points, count);
  const int samples = g_antialias ? kRasterSamples : 1;
  int edge_count = 0;
  int first_row = g_height * samples;
  int last_row = 0;
  for (int index = 0; index < count; ++index) {
    const Point &a = points[index];
    const Point &b = points[(index + 1) % count];
    const float low = std::min(a.y, b.y);
    const float high = std::max(a.y, b.y);
    // The optional two-subrow path softens diagonal and near-horizontal
    // contours without allocating a larger framebuffer.
    const int start = ceil_raster(low * samples - 0.5F,0,g_height * samples);
    const int end = ceil_raster(high * samples - 0.5F,0,g_height * samples);
    if (start >= end || edge_count >= static_cast<int>(std::size(g_raster_edges))) {
      continue;
    }
    const float inverse_height = 1.0F / (b.y - a.y);
    g_raster_edges[edge_count++] = {
        start,
        end,
        a.x + (b.x - a.x) *
                  ((start + 0.5F) / samples - a.y) * inverse_height,
        (b.x - a.x) * inverse_height / samples,
    };
    first_row = std::min(first_row, start);
    last_row = std::max(last_row, end);
  }
  if (edge_count == 0) return;
  std::sort(g_raster_edges, g_raster_edges + edge_count,
            [](const RasterEdge &left, const RasterEdge &right) {
              return left.first_row < right.first_row;
            });

  int pending_edge = 0;
  int active_count = 0;
  int current_y = -1;
  for (int sample_row = first_row; sample_row < last_row; ++sample_row) {
    const int y = sample_row / samples;
    if (g_antialias && y != current_y) {
      if (current_y >= 0) paint_coverage_row(current_y, color, clip_to_body);
      begin_coverage_row();
      current_y = y;
    }
    while (pending_edge < edge_count &&
           g_raster_edges[pending_edge].first_row <= sample_row) {
      g_active_edges[active_count++] = pending_edge++;
    }
    int hits = 0;
    int retained = 0;
    for (int active = 0; active < active_count; ++active) {
      const int edge_index = g_active_edges[active];
      const RasterEdge &edge = g_raster_edges[edge_index];
      if (sample_row >= edge.end_row) continue;
      g_active_edges[retained++] = edge_index;
      if (hits < static_cast<int>(std::size(g_crossings))) {
        g_crossings[hits++] =
            edge.first_x + edge.x_step * (sample_row - edge.first_row);
      }
    }
    active_count = retained;
    // The radial body and eye contours normally intersect a scanline only
    // twice. Avoid invoking the generic sort on that overwhelmingly common
    // hot path; complex/concave contours still retain the general fallback.
    if (hits == 2) {
      if (g_crossings[0] > g_crossings[1]) {
        std::swap(g_crossings[0], g_crossings[1]);
      }
    } else if (hits > 2) {
      std::sort(g_crossings, g_crossings + hits);
    }
    for (int hit = 0; hit + 1 < hits; hit += 2) {
      if (g_antialias) {
        add_coverage_span(g_crossings[hit], g_crossings[hit + 1],
                          1.0F / kRasterSamples);
      } else {
        const int left = ceil_raster(g_crossings[hit] - 0.5F,0,g_width);
        const int right = ceil_raster(g_crossings[hit + 1] - 0.5F,left,g_width);
        uint16_t *const row = g_frame + y * g_stride;
        if (clip_to_body) {
          for (int x = left; x < right; ++x)
            if (row[x] == g_body_wire_color) row[x] = color;
        } else {
          std::fill(row + left, row + right, color);
        }
      }
    }
  }
  if (g_antialias && current_y >= 0)
    paint_coverage_row(current_y, color, clip_to_body);
}

float shape_radius(int shape, int point, int other_shape, float mix,
                   float roundness) {
  const auto radius_for = [point](int value) {
    if (value <= 0) return 1.0F;
    return lunaredu::pet::render::shape_profile[
        std::clamp(value - 1, 0, lunaredu::pet::render::ShapeCount - 2)][point];
  };
  const float from = radius_for(other_shape);
  const float to = radius_for(shape);
  const float morphed =
      from + (to - from) * std::clamp(mix, 0.0F, 1.0F);
  return morphed + (1.0F - morphed) *
                        std::clamp(roundness, 0.0F, 1.0F);
}

Transform body_transform(const PetFrame &g) {
  const int shape = std::clamp(g.shape_to, 0, lunaredu::pet::render::ShapeCount - 1);
  const float pose_scale = kShapeZoom[shape] * kViewScale * g_content_scale;
  const float body_radius = kSourceRadius *
      std::min(g_width, g_height) / kSourceView * pose_scale;
  const float cosine = std::cos(g.roll);
  const float sine = std::sin(g.roll);
  const float raw_turn_scale = std::cos(g.turn);
  const float turn_scale = std::copysign(
      std::max(0.12F, std::abs(raw_turn_scale)), raw_turn_scale);
  const float pitch_scale = std::max(0.16F, std::abs(std::cos(g.pitch)));
  const float scale_x = body_radius * g.body_scale * turn_scale;
  const float scale_y = body_radius * g.body_scale *
                        std::max(0.12F, g.squash) * pitch_scale;
  Transform transform;
  transform.center_x = g_width * 0.5F + g.offset[0] * body_radius;
  transform.center_y = g_height * 0.5F - g.offset[1] * body_radius;
  transform.xx = cosine * scale_x;
  transform.xy = -sine * scale_y;
  transform.yx = sine * scale_x;
  transform.yy = cosine * scale_y;
  return transform;
}

void draw_body(const PetFrame &g, const Transform &transform,
               uint16_t color) {
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::Body);
  // Advancing around the contour by a fixed complex rotation avoids 192
  // software trig calls per frame on the ESP32-S3.
  constexpr float kStepCos = 0.9978589232F;
  constexpr float kStepSin = 0.0654031292F;
  if (!g_body_contour_key.valid || g_body_contour_key.to!=g.shape_to ||
      g_body_contour_key.from!=g.shape_from || g_body_contour_key.mix!=g.shape_mix ||
      g_body_contour_key.roundness!=g.roundness) {
    float unit_x = 1.0F, unit_y = 0.0F;
    for (int index = 0; index < kBodyPointCount; ++index) {
      const float radius = shape_radius(g.shape_to, index, g.shape_from,
                                        g.shape_mix, g.roundness);
      g_body_local[index] = {unit_x * radius, unit_y * radius};
      const float next_x = unit_x * kStepCos - unit_y * kStepSin;
      unit_y = unit_y * kStepCos + unit_x * kStepSin;
      unit_x = next_x;
    }
    g_body_contour_key={g.shape_to,g.shape_from,g.shape_mix,g.roundness,true};
  }
  // Pose is never cached: effects also consume the actual transformed contour.
  for (int index = 0; index < kBodyPointCount; ++index)
    g_body_points[index] = apply(transform,g_body_local[index].x,g_body_local[index].y);
  const int shape = std::clamp(g.shape_to, 0, lunaredu::pet::render::ShapeCount - 1);
  if (g.shape_mix >= 0.999F && g.roundness <= 0.001F &&
      g.overlay <= 0.001F) {
    const int count = lunaredu::pet::render::path_meta[shape].count;
    for (int point = 0; point < count; ++point) {
      g_path_points[point] = apply(transform,
          lunaredu::pet::render::path_points[shape][point][0],
          lunaredu::pet::render::path_points[shape][point][1]);
    }
    fill_polygon(g_path_points, count, color);
  } else {
    fill_polygon(g_body_points, kBodyPointCount, color);
  }
}

// The source eye paths are already normalized about GROK_GEO.Re in assets.hpp.
// This is the same placement pipeline as runtime/eyes.js paintEyes: face tune,
// available-gap fit, face placement, gaze, body span and blink. Only SVG path
// rasterization is replaced with scanline polygons.
Point body_span(float y, int stable_shape) {
  if (stable_shape >= 0) {
    const lunaredu::pet::render::PathMeta &meta = lunaredu::pet::render::path_meta[stable_shape];
    const float position = std::clamp(
        (y - meta.top) / (meta.bottom - meta.top) *
            lunaredu::pet::render::PathSpanCount - 0.5F,
        0.0F, float(lunaredu::pet::render::PathSpanCount - 1));
    const int first = static_cast<int>(position);
    const int second = std::min(first + 1, lunaredu::pet::render::PathSpanCount - 1);
    const float fraction = position - first;
    return {
        lunaredu::pet::render::path_spans[stable_shape][first][0] +
            (lunaredu::pet::render::path_spans[stable_shape][second][0] -
             lunaredu::pet::render::path_spans[stable_shape][first][0]) * fraction,
        lunaredu::pet::render::path_spans[stable_shape][first][1] +
            (lunaredu::pet::render::path_spans[stable_shape][second][1] -
             lunaredu::pet::render::path_spans[stable_shape][first][1]) * fraction};
  }
  float left = -INFINITY;
  float right = INFINITY;
  for (int point = 0; point < kBodyPointCount; ++point) {
    const Point &a = g_body_local[point];
    const Point &b = g_body_local[(point + 1) % kBodyPointCount];
    if ((a.y <= y) == (b.y <= y)) continue;
    const float x = a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y);
    if (x <= 0.0F) left = std::max(left, x);
    else right = std::min(right, x);
  }
  return {std::isfinite(left) ? left : 0.0F,
          std::isfinite(right) ? right : 0.0F};
}

void draw_eyes(const PetFrame &g, const Transform &transform,
               uint16_t color) {
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::Eyes);
  if (g.overlay >= 0.5F) return;
  auto &polys=g_eye_polys;
  auto &cents=g_eye_centers;
  if(!g_eye_weights_valid || std::memcmp(g_eye_weights,g.weights,sizeof(g_eye_weights))!=0) {
    int active_moods[25];
    int active_count = 0;
    for (int mood = 0; mood < 25; ++mood) {
      if (g.weights[mood] > 0.00001F) active_moods[active_count++] = mood;
    }
    g_eye_centers[0]={};g_eye_centers[1]={};
    for (int eye = 0; eye < 2; ++eye) {
      for (int point = 0; point < kEyePointCount; ++point) {
        Point value{};
        for (int active = 0; active < active_count; ++active) {
          const int mood = active_moods[active];
          const float weight = g.weights[mood];
          value.x += lunaredu::pet::render::eyes[mood][eye][point][0] * weight;
          value.y += lunaredu::pet::render::eyes[mood][eye][point][1] * weight;
        }
        polys[eye][point] = value;
        cents[eye].x += value.x / kEyePointCount;
        cents[eye].y += value.y / kEyePointCount;
      }
    }
    std::memcpy(g_eye_weights,g.weights,sizeof(g_eye_weights));g_eye_weights_valid=true;
  }
  float top = INFINITY;
  float bottom = -INFINITY;
  for (const Point &point : g_body_local) {
    top = std::min(top, point.y);
    bottom = std::max(bottom, point.y);
  }
  float face[6];
  const int from = std::clamp(g.shape_from, 0, lunaredu::pet::render::ShapeCount - 1);
  const int to = std::clamp(g.shape_to, 0, lunaredu::pet::render::ShapeCount - 1);
  const int stable_shape = g.shape_mix >= 0.999F &&
      g.roundness <= 0.001F ? to : -1;
  if (stable_shape >= 0) {
    top = lunaredu::pet::render::path_meta[to].top;
    bottom = lunaredu::pet::render::path_meta[to].bottom;
  }
  const float shape_mix = std::clamp(g.shape_mix, 0.0F, 1.0F);
  for (int index = 0; index < 6; ++index) {
    face[index] = lunaredu::pet::render::shape_face[from][index] * (1.0F - shape_mix) +
                  lunaredu::pet::render::shape_face[to][index] * shape_mix;
  }
  const float face_sx = face[2] * 1.18F;
  const float face_sy = face[3];
  const float s_x = face[5]; // UNIFORM_EYES=true
  float left_extent = 0.0F;
  float right_extent = 0.0F;
  for (int point = 0; point < kEyePointCount; ++point) {
    left_extent = std::max(left_extent,
                           std::abs(polys[0][point].x - cents[0].x));
    right_extent = std::max(right_extent,
                            std::abs(polys[1][point].x - cents[1].x));
  }
  const float gap = std::abs(cents[1].x - (cents[0].x + s_x)) * face_sx;
  const float available = left_extent + right_extent > 0.5F / kSourceRadius
      ? std::clamp(gap / (left_extent + right_extent), 0.35F, 4.0F)
      : 4.0F;
  const float pulse = 1.0F + 0.07F *
      std::sin(std::clamp(g.eye_morph, 0.0F, 1.0F) * kPi);
  const float eye_scale_prop = kShapeZoom[1] / kShapeZoom[to];
  const float uniform_eye = eye_scale_prop; // face.eye is ignored by source
  const float scale = std::min(std::clamp(g.eye_scale, 0.2F, 2.0F) *
                                   uniform_eye, available / pulse);
  const float width = std::min(scale * 0.96F, available / pulse);
  const float height = scale * 0.92F;
  const float cr[] = {
      0.981789481F, 0.056442274F, -0.181393727F,
      -0.094798422F, 0.973023539F, -0.210329391F,
      0.164628897F, 0.223695022F, 0.960654914F};
  for (int eye = 0; eye < 2; ++eye) {
    const Point c = cents[eye];
    const float ea = c.x + (eye == 0 ? s_x : 0.0F);
    const float xr = ea;
    const float fr = -c.y;
    const float ia = std::sqrt(std::max(0.0F, 1 - xr * xr - fr * fr));
    const float z = ia > 0.0F ? ia : 0.02F;
    const float li = cr[0] * xr + cr[1] * fr + cr[2] * z;
    const float bl = cr[3] * xr + cr[4] * fr + cr[5] * z;
    const float io = cr[6] * xr + cr[7] * fr + cr[8] * z;
    const float wo = li * face_sx;
    const float sre = std::clamp(face[1] - bl * face_sy,
                                 top + 2.0F / kSourceRadius,
                                 bottom - 2.0F / kSourceRadius);
    float uo = -fr * xr;
    float tl = 1 - fr * fr;
    float zi = -fr * z;
    const float norm = std::hypot(uo, std::hypot(tl, zi));
    if (norm < 0.000001F) { uo = 0; tl = 0; zi = 1; }
    else { uo /= norm; tl /= norm; zi /= norm; }
    const float md = fr * zi - z * tl;
    const float oc = z * uo - xr * zi;
    const float yu = xr * tl - fr * uo;
    const float vm = cr[0] * uo + cr[1] * tl + cr[2] * zi;
    const float hm = cr[3] * uo + cr[4] * tl + cr[5] * zi;
    const float nr = cr[0] * md + cr[1] * oc + cr[2] * yu;
    const float er = cr[3] * md + cr[4] * oc + cr[5] * yu;
    const float ku = md * -tl - uo * -oc;
    const float inv = 1.0F / (std::abs(ku) < 0.000001F ? 0.000001F : ku);
    const float ql = -tl * inv;
    const float ge = -uo * inv;
    const float bm = oc * inv;
    const float ir = md * inv;
    const float km = nr * ql + vm * bm;
    const float fee = nr * ge + vm * ir;
    const float ree = -er * ql - hm * bm;
    const float zee = -er * ge - hm * ir;
    if (io <= 0.02F) continue;
    const float tre_x = std::clamp(io / 0.5F, 0.0F, 1.0F);
    const float tre = tre_x * tre_x * (3 - 2 * tre_x);
    const float now = g.time * 1000.0F;
    const float kj = (std::sin(now * 0.00042F + eye) * 1.4F +
        std::sin(now * 0.001F + eye * 2) * 0.5F) / kSourceRadius +
        g.gaze[0] - 10.0F * std::clamp(g.notify, 0.0F, 1.0F) /
            kSourceRadius;
    const float ko = std::sin(now * 0.00058F + eye) * 0.9F /
        kSourceRadius + g.gaze[1] +
        7.0F * std::clamp(g.notify, 0.0F, 1.0F) / kSourceRadius;
    const float vee = std::clamp(std::max(std::hypot(km, ree), 0.02F) *
                                 width * pulse, 0.02F, 2.4F);
    const float lid = std::max(g.lids[eye], 0.04F);
    const float eye_h = std::clamp(std::max(std::hypot(fee, zee), 0.02F) *
                                   lid * height * pulse, 0.02F, 2.4F);
    const float margin = 21.0F / kSourceRadius * eye_h +
        2.0F / kSourceRadius;
    const float vl = std::clamp(sre + ko * face_sy,
                                top + margin, bottom - margin);
    float bound_left = -INFINITY;
    float bound_right = INFINITY;
    for (int point = 0; point < kEyePointCount; point += 2) {
      const float dx = (polys[eye][point].x - c.x) * vee;
      const Point span = body_span(vl +
          (polys[eye][point].y - c.y) * eye_h, stable_shape);
      bound_left = std::max(bound_left, span.x - dx);
      bound_right = std::min(bound_right, span.y - dx);
    }
    const float wanted = face[0] + wo + kj * face_sx;
    const float fitted = bound_left <= bound_right
        ? std::clamp(wanted, bound_left, bound_right)
        : (bound_left + bound_right) * 0.5F;
    const float dd = fitted + (wanted - fitted) * (1.0F - tre);
    const float fr_m = std::clamp(width * pulse, 0.02F, 2.4F);
    const float ia_m = std::clamp(lid * height * pulse, 0.02F, 2.4F);
    Point polygon[kEyePointCount];
    for (int point = 0; point < kEyePointCount; ++point) {
      const float dx = polys[eye][point].x - c.x;
      const float dy = polys[eye][point].y - c.y;
      polygon[point] = apply(transform,
          dd + km * fr_m * dx + fee * ia_m * dy,
          vl + ree * fr_m * dx + zee * ia_m * dy);
    }
    fill_polygon(polygon, kEyePointCount, color, true);
  }
}

uint32_t eye_rgb(uint32_t) {
  // CharacterMotionLab's light scheme sets --bg to GROK_TABLES.EYE_BG.
  // Choosing eye pigment from body luminance turned blue pets' eyes black.
  return 0xf3efe6U;
}

} // namespace

static void render_frame(PetFrameBuffer frame, const PetRenderSnapshot *snapshot,
                         int status, bool cleared_by_owner) {
  if (frame.pixels == nullptr || frame.width <= 0 || frame.height <= 0 ||
      frame.stride < frame.width) return;
  const uint64_t started = profile_now_us();
  static const PetRenderSnapshot empty{};
  if (snapshot == nullptr) snapshot = &empty;
  const bool geometry_changed = g_width != frame.width ||
                                g_height != frame.height ||
                                g_stride != frame.stride ||
                                g_body_diameter != frame.body_diameter ||
                                g_antialias != (frame.antialias != 0) ||
                                g_native_rgb565 != (frame.native_rgb565 != 0);
  g_native_rgb565=frame.native_rgb565!=0;
  g_frame = frame.pixels;
  g_width = frame.width;
  g_height = frame.height;
  g_stride = frame.stride;
  g_body_diameter = frame.body_diameter;
  g_antialias = frame.antialias != 0;
  if (geometry_changed) {
    const float source_blob_diameter = 2.0F * kSourceRadius *
        std::min(g_width, g_height) / kSourceView * kShapeZoom[1] * kViewScale;
    g_content_scale = g_body_diameter > 0
        ? g_body_diameter / source_blob_diameter : 1.0F;
    pet_render_invalidate();
  }
  PixelHistory *history = nullptr;
  for (auto &candidate : g_pixel_history) {
    if (candidate.pixels == frame.pixels) {
      history = &candidate;
      break;
    }
  }
  const bool first_use = history == nullptr;
  if (first_use) {
    history = &g_pixel_history[g_next_pixel_history++ %
                               std::size(g_pixel_history)];
    *history = {frame.pixels, {}};
  }
  g_visible_bounds = {};
  if (!cleared_by_owner && first_use) {
    if(g_stride==g_width)std::memset(g_frame,0,size_t(g_width)*g_height*sizeof(uint16_t));
    else for (int y = 0; y < g_height; ++y)
      std::memset(g_frame+y*g_stride,0,size_t(g_width)*sizeof(uint16_t));
  } else if (!cleared_by_owner && history->bounds.width > 0 && history->bounds.height > 0) {
    const int left = std::clamp(history->bounds.x, 0, g_width);
    const int top = std::clamp(history->bounds.y, 0, g_height);
    const int right = std::clamp(
        history->bounds.x + history->bounds.width, left, g_width);
    const int bottom = std::clamp(
        history->bounds.y + history->bounds.height, top, g_height);
    for (int y = top; y < bottom; ++y) {
      std::memset(g_frame+y*g_stride+left,0,size_t(right-left)*sizeof(uint16_t));
    }
  }
  const uint64_t cleared = profile_now_us();

  const PetFrame &g = snapshot->frame;
  g_touch_frame = g;
  g_touch_frame_valid = g.active != 0;
  uint64_t body_done = cleared;
  uint64_t eyes_done = cleared;
  uint64_t back_ribbon_done = cleared;
  uint64_t effects_done = cleared;
  uint64_t eye_shape_done = cleared;
  bool task_profiled = false;
  uint32_t task_effects_begin=0,task_body_begin=0,task_eyes_begin=0,task_eyes_done=0;
#ifdef ESP_PLATFORM
  uint32_t task_begin=0;
  task_profiled=lunaredu::system::worker_work_profiling_active();
  task_begin=lunaredu::system::current_task_runtime_us(task_profiled);
#endif
  if (g.active) {
    if (g.effect_state != g_effect_state) {
      // Keep the outgoing pencil trail alive for its fade-out. Starting a
      // fresh pencil effect still begins with a clean canvas.
      if (g.effect_state == lunaredu::pet::render::writing ||
          g.previous_effect_state != lunaredu::pet::render::writing) g_ink_count = 0;
      g_effect_state = g.effect_state;
    }
    const uint32_t body_rgb = g.color_custom ? g.body_color : 0x2a92feU;
    const Transform transform = body_transform(g);
    const float opacity = std::max(g.opacity, g.overlay * 0.18F);
    draw_state_ribbons(g, false);
    back_ribbon_done = profile_now_us();
#ifdef ESP_PLATFORM
    task_effects_begin=lunaredu::system::current_task_runtime_us(task_profiled);
#endif
    if (g.overlay > 0.004F && g.effect_state >= 0) {
      if (g.previous_effect_state >= 0 && g.overlay_mix < 0.996F) {
        PetFrame previous = g;
        previous.state = g.previous_effect_state;
        previous.state_time = g.previous_effect_state_time;
        previous.overlay_time = g.previous_overlay_time;
        previous.fx_receive_direction = g.previous_receive_direction;
        previous.progress_fraction = g.previous_progress_fraction;
        previous.overlay *= 1.0F - g.overlay_mix;
        if (previous.overlay > 0.004F) draw_state_effects(previous, body_rgb);
      }
      PetFrame current = g;
      current.state = g.effect_state;
      current.state_time = g.effect_state_time;
      current.overlay *= g.overlay_mix;
      if (current.overlay > 0.004F) draw_state_effects(current, body_rgb);
    }
    effects_done = profile_now_us();
#ifdef ESP_PLATFORM
    task_body_begin=lunaredu::system::current_task_runtime_us(task_profiled);
#endif
    g_body_wire_color = wire_rgb565(body_rgb, opacity);
    draw_body(g, transform, g_body_wire_color);
    body_done = profile_now_us();
#ifdef ESP_PLATFORM
    task_eyes_begin=lunaredu::system::current_task_runtime_us(task_profiled);
#endif
    if (g.overlay < 0.995F) {
      draw_eyes(g, transform, wire_rgb565(eye_rgb(body_rgb), opacity));
    }
    if (g.state == lunaredu::pet::render::notifying) {
      draw_state_effects(g, body_rgb);
    }
    eye_shape_done = profile_now_us();
#ifdef ESP_PLATFORM
    task_eyes_done=lunaredu::system::current_task_runtime_us(task_profiled);
#endif
    draw_state_ribbons(g, true);
    eyes_done = profile_now_us();
  }

  if (status != 0) {
    const uint16_t ink =
        wire_rgb565(status == 1 ? 0xb99a5eU : 0xd5683eU);
    const int dot_size = std::max(4, g_height / 60);
    const int dot_gap = std::max(10, g_height / 24);
    const int status_width = dot_size * 3 + (dot_gap - dot_size) * 2;
    const int status_x = (g_width - status_width) / 2;
    const int status_y = g_height - std::max(15, g_height / 16);
    for (int dot = 0; dot < 3; ++dot) {
      const int x = status_x + dot * dot_gap;
      for (int y = status_y; y < status_y + dot_size; ++y) {
        std::fill(g_frame + y * g_stride + x,
                  g_frame + y * g_stride + x + dot_size, ink);
      }
    }
    const Point status_bounds[] = {
        {static_cast<float>(status_x), static_cast<float>(status_y)},
        {static_cast<float>(status_x + status_width),
         static_cast<float>(status_y + dot_size)}};
    include_bounds(status_bounds, 2);
  }
  const uint64_t finished = profile_now_us();
  g_render_profile = {
      static_cast<uint32_t>(cleared - started),
      static_cast<uint32_t>(body_done - cleared),
      static_cast<uint32_t>(eyes_done - body_done),
      static_cast<uint32_t>(finished - started),
      static_cast<uint32_t>(back_ribbon_done - cleared),
      static_cast<uint32_t>(effects_done - back_ribbon_done),
      static_cast<uint32_t>(body_done - effects_done),
      static_cast<uint32_t>(eye_shape_done - body_done),
      static_cast<uint32_t>(eyes_done - eye_shape_done),
      0,0,0,0,0,
  };
  history->bounds = g_visible_bounds;
  if(task_profiled) {
#ifdef ESP_PLATFORM
    g_render_profile.total_task_us=uint32_t(lunaredu::system::current_task_runtime_us(true)-task_begin);
#endif
    g_render_profile.body_task_us=uint32_t(task_eyes_begin-task_body_begin);
    g_render_profile.eyes_task_us=uint32_t(task_eyes_done-task_eyes_begin);
    g_render_profile.effects_task_us=uint32_t(task_body_begin-task_effects_begin);
    g_render_profile.task_profiled=1;
  }
}

extern "C" void pet_render_frame(PetFrameBuffer frame, const PetRenderSnapshot *snapshot, int status) {
  render_frame(frame, snapshot, status, false);
}
extern "C" void pet_render_frame_cleared(PetFrameBuffer frame, const PetRenderSnapshot *snapshot, int status) {
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::Character);
  render_frame(frame, snapshot, status, true);
}

extern "C" void pet_render_invalidate() {
  for (auto &history : g_pixel_history) history = {};
  g_next_pixel_history = 0;
  g_touch_frame_valid = false;
  g_ink_count = 0;
  g_effect_state = -1;
}

extern "C" PetRegion pet_render_bounds() { return g_visible_bounds; }

extern "C" PetRenderProfile pet_render_profile() {
  return g_render_profile;
}

extern "C" int pet_render_project_touch(int x, int y, float *local_x,
                                        float *local_y) {
  if (!g_touch_frame_valid || g_width <= 0 || g_height <= 0) return 0;
  const Transform transform = body_transform(g_touch_frame);
  const float determinant =
      transform.xx * transform.yy - transform.xy * transform.yx;
  if (std::abs(determinant) < 0.0001F) return 0;
  const float dx = static_cast<float>(x) - transform.center_x;
  const float dy = static_cast<float>(y) - transform.center_y;
  const float projected_x =
      (transform.yy * dx - transform.xy * dy) / determinant;
  const float projected_y =
      (-transform.yx * dx + transform.xx * dy) / determinant;
  const float angle = std::atan2(projected_y, projected_x);
  const float turn = angle < 0.0F ? angle + 2.0F * kPi : angle;
  const int point = std::clamp(
      static_cast<int>(std::lround(turn * kBodyPointCount / (2.0F * kPi))) %
          kBodyPointCount,
      0, kBodyPointCount - 1);
  const float radius = shape_radius(
      g_touch_frame.shape_to, point, g_touch_frame.shape_from,
      g_touch_frame.shape_mix, g_touch_frame.roundness);
  if (local_x != nullptr) *local_x = projected_x / std::max(radius, 0.1F);
  if (local_y != nullptr) *local_y = projected_y / std::max(radius, 0.1F);
  const float distance =
      std::sqrt(projected_x * projected_x + projected_y * projected_y);
  return distance <= radius * 1.08F ? 1 : 0;
}
