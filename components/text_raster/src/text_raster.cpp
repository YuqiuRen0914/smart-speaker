#include "lunaros_text_raster.h"
#include "key_index.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <cstring>
#include <mutex>
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype_htcw.h>

namespace lunaredu::lunaros::text_raster {
static void clear_glyph_cache_unlocked();
static bool glyph_metrics_unlocked(uint32_t,unsigned,GlyphMetrics &);
namespace {
constexpr int kGlyphLimit = 64;
std::mutex g_mutex; // Protect coverage and cache compaction across display callers.
stbtt_fontinfo g_font{};
bool g_ready = false;
FontSource g_source;
int g_ascent = 0;
int g_descent = 0;
int g_line_gap = 0;
// Display clients serialize each operation; retain one shared PSRAM cache and
// keep glyph scratch off managed task stacks.
#ifdef ESP_PLATFORM
EXT_RAM_BSS_ATTR
#endif
std::array<uint8_t, kGlyphLimit * kGlyphLimit> g_alpha{};
struct Glyph {
  uint32_t codepoint = 0, pixel_size = 0, used_at = 0, offset = 0;
  int left = 0, top = 0, advance = 0;
  uint16_t width = 0, height = 0, bytes = 0;
};
// Cache metadata has the same display-owner access as the external alpha
// scratch and bitmap pool; it does not require scarce internal/DMA RAM.
#ifdef ESP_PLATFORM
EXT_RAM_BSS_ATTR
#endif
std::array<Glyph, kGlyphCacheEntryLimit> g_glyphs{};
struct Metric { uint32_t codepoint=0; unsigned size=0; GlyphMetrics value{}; };
#ifdef ESP_PLATFORM
EXT_RAM_BSS_ATTR
#endif
std::array<Metric,256> g_metrics{};
#ifdef ESP_PLATFORM
EXT_RAM_BSS_ATTR
#endif
detail::KeyIndex<kGlyphCacheEntryLimit> g_glyph_index;
#ifdef ESP_PLATFORM
EXT_RAM_BSS_ATTR
#endif
detail::KeyIndex<256> g_metric_index;
size_t g_metric_next=0;
uint8_t *g_cache = nullptr;
size_t g_capacity = 0, g_end = 0, g_used = 0, g_entries = 0;
uint32_t g_clock = 0;
std::atomic<bool> g_cache_enabled{false};
std::atomic<uint32_t> g_font_generation{0};
std::atomic<size_t> g_stat_capacity{0}, g_stat_used{0}, g_stat_entries{0};
std::atomic<uint32_t> g_hits{0}, g_misses{0}, g_rasterizations{0}, g_evictions{0};

void publish_occupancy() {
  g_stat_used.store(g_used); g_stat_entries.store(g_entries);
}
void evict(Glyph &glyph) {
  g_glyph_index.erase(glyph.codepoint,glyph.pixel_size,unsigned(&glyph-g_glyphs.data()));
  g_used -= glyph.bytes; --g_entries; glyph = {}; ++g_evictions;
}
Glyph &oldest() {
  Glyph *result = nullptr;
  for (auto &glyph : g_glyphs)
    if (glyph.pixel_size && (!result ||
        uint32_t(g_clock - glyph.used_at) > uint32_t(g_clock - result->used_at))) result = &glyph;
  return *result; // Called only while at least one entry exists.
}
void compact() {
  std::array<Glyph *, kGlyphCacheEntryLimit> order{};
  size_t count = 0;
  for (auto &glyph : g_glyphs) if (glyph.pixel_size && glyph.bytes) order[count++] = &glyph;
  std::sort(order.begin(), order.begin() + count,
            [](const Glyph *a, const Glyph *b) { return a->offset < b->offset; });
  g_end = 0;
  for (size_t i = 0; i < count; ++i) {
    auto &glyph = *order[i];
    if (glyph.offset != g_end)
      std::memmove(g_cache + g_end, g_cache + glyph.offset, glyph.bytes);
    glyph.offset = static_cast<uint32_t>(g_end); g_end += glyph.bytes;
  }
}
Glyph *retain(const Glyph &source) {
  if (!g_cache || source.bytes > g_capacity) return nullptr;
  Glyph *slot = nullptr;
  for (auto &glyph : g_glyphs) if (!glyph.pixel_size) { slot = &glyph; break; }
  if (!slot) { slot = &oldest(); evict(*slot); }
  while (g_used + source.bytes > g_capacity) evict(oldest());
  if (g_end + source.bytes > g_capacity) compact();
  *slot = source; slot->offset = static_cast<uint32_t>(g_end);
  if(g_glyph_index.needs_rebuild()){
    g_glyph_index.clear();
    for(unsigned i=0;i<g_glyphs.size();++i)if(g_glyphs[i].pixel_size && &g_glyphs[i]!=slot)
      g_glyph_index.insert(g_glyphs[i].codepoint,g_glyphs[i].pixel_size,i);
  }
  g_glyph_index.insert(slot->codepoint,slot->pixel_size,unsigned(slot-g_glyphs.data()));
  if (source.bytes) std::memcpy(g_cache + g_end, g_alpha.data(), source.bytes);
  g_end += source.bytes; g_used += source.bytes; ++g_entries;
  publish_occupancy(); return slot;
}

uint32_t next_codepoint(const char *&cursor) {
  const uint8_t first = static_cast<uint8_t>(*cursor++);
  if (first < 0x80) return first;
  const int count = (first & 0xf8) == 0xf0 ? 3 :
                    (first & 0xf0) == 0xe0 ? 2 :
                    (first & 0xe0) == 0xc0 ? 1 : 0;
  if (count == 0) return '?';
  uint32_t value = first & ((1U << (6 - count)) - 1U);
  for (int i = 0; i < count; ++i) {
    const uint8_t continuation = static_cast<uint8_t>(*cursor);
    if (continuation == 0 || (continuation & 0xc0) != 0x80) return '?';
    value = (value << 6) | (continuation & 0x3f);
    ++cursor;
  }
  return value;
}

float scale(unsigned size) {
  return stbtt_ScaleForMappingEmToPixels(&g_font, static_cast<float>(size));
}

int advance(uint32_t codepoint, float glyph_scale) {
  int units = 0, bearing = 0;
  stbtt_GetCodepointHMetrics(&g_font, static_cast<int>(codepoint), &units,
                             &bearing);
  return std::max(1, static_cast<int>(std::lround(units * glyph_scale)));
}

Glyph glyph_for(uint32_t codepoint, unsigned pixel_size, float glyph_scale,
                const uint8_t *&coverage) {
  if (++g_clock == 0) { clear_glyph_cache_unlocked(); g_clock = 1; }
  const int found=g_cache?g_glyph_index.find(codepoint,pixel_size,[&](unsigned i){return g_glyphs[i].pixel_size==pixel_size&&g_glyphs[i].codepoint==codepoint;}):-1;
  if(found>=0){auto &glyph=g_glyphs[unsigned(found)];
      glyph.used_at = g_clock; ++g_hits;
      coverage = g_cache + glyph.offset; return glyph;
  }
  ++g_misses;
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  stbtt_GetCodepointBitmapBox(&g_font, static_cast<int>(codepoint), glyph_scale,
                            glyph_scale, &x0, &y0, &x1, &y1);
  Glyph glyph;
  glyph.codepoint = codepoint; glyph.pixel_size = pixel_size; glyph.used_at = g_clock;
  glyph.left = x0; glyph.top = y0;
  glyph.advance = advance(codepoint, glyph_scale);
  if (x1 > x0 && y1 > y0 && x1 - x0 <= kGlyphLimit && y1 - y0 <= kGlyphLimit) {
    glyph.width = static_cast<uint16_t>(x1 - x0); glyph.height = static_cast<uint16_t>(y1 - y0);
    glyph.bytes = glyph.width * glyph.height;
    stbtt_MakeCodepointBitmap(&g_font, g_alpha.data(), glyph.width, glyph.height,
                              glyph.width, glyph_scale, glyph_scale, static_cast<int>(codepoint));
    ++g_rasterizations;
  }
  if (const auto *cached = retain(glyph)) {
    coverage = g_cache + cached->offset; return *cached;
  }
  coverage = g_alpha.data(); return glyph;
}
}  // namespace

bool initialize(const uint8_t *font_data, size_t font_size) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_ready) return true;
  if (font_data == nullptr || font_size < 12) return false;
  const int offset = stbtt_GetFontOffsetForIndex(font_data, 0);
  if (offset < 0 || static_cast<size_t>(offset) >= font_size ||
      !stbtt_InitFont(&g_font, font_data, offset)) return false;
  stbtt_GetFontVMetrics(&g_font, &g_ascent, &g_descent, &g_line_gap);
  g_ready = g_ascent > g_descent;
  if(g_ready){g_source={font_data,font_size,g_font_generation.fetch_add(1,std::memory_order_release)+1};}
  return g_ready;
}

bool ready() { std::lock_guard<std::mutex> lock(g_mutex); return g_ready; }
uint32_t font_generation(){return g_font_generation.load(std::memory_order_acquire);}
FontSource font_source(){std::lock_guard<std::mutex> lock(g_mutex);return g_source;}

bool configure_glyph_cache(uint8_t *storage, size_t bytes) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if ((!storage && bytes) || (storage && !bytes) || bytes > kGlyphCacheByteLimit) return false;
  g_cache = storage; g_capacity = bytes;
  clear_glyph_cache_unlocked();
  g_hits.store(0); g_misses.store(0); g_rasterizations.store(0); g_evictions.store(0);
  g_stat_capacity.store(bytes); g_cache_enabled.store(storage != nullptr);
  return true;
}
static void clear_glyph_cache_unlocked() {
  g_glyphs = {}; g_end = g_used = g_entries = 0; g_clock = 0;
  g_glyph_index.clear();
  publish_occupancy();
}
void clear_glyph_cache() {
  std::lock_guard<std::mutex> lock(g_mutex);
  clear_glyph_cache_unlocked();
}
GlyphCacheStats glyph_cache_stats() {
  return {g_cache_enabled.load(), g_stat_capacity.load(), g_stat_used.load(),
          g_stat_entries.load(), g_hits.load(), g_misses.load(),
          g_rasterizations.load(), g_evictions.load()};
}

int width(const char *utf8, unsigned pixel_size) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_ready || utf8 == nullptr || pixel_size == 0) return 0;
  const float glyph_scale = scale(pixel_size);
  int result = 0;
  for (const char *cursor = utf8; *cursor != '\0';){
    const auto cp=next_codepoint(cursor);GlyphMetrics metric;
    result+=glyph_metrics_unlocked(cp,pixel_size,metric)?metric.advance:advance(cp,glyph_scale);
  }
  return result;
}

int line_height(unsigned pixel_size) {
  // Immutable after setup; callbacks may inspect line metrics while drawing.
  if (!g_ready || pixel_size == 0) return 0;
  return std::max(1, static_cast<int>(std::lround(
      (g_ascent - g_descent + g_line_gap) * scale(pixel_size))));
}

int baseline(unsigned pixel_size) {
  return g_ready&&pixel_size?static_cast<int>(std::lround(g_ascent*scale(pixel_size))):0;
}
static bool glyph_metrics_unlocked(uint32_t codepoint,unsigned pixel_size,GlyphMetrics &out) {
  out={};if(!g_ready||!pixel_size||pixel_size>64)return false;
  const int found=g_metric_index.find(codepoint,pixel_size,[&](unsigned i){return g_metrics[i].size==pixel_size&&g_metrics[i].codepoint==codepoint;});
  if(found>=0){out=g_metrics[unsigned(found)].value;return true;}
  const auto factor=scale(pixel_size);
  int x0=0,y0=0,x1=0,y1=0;
  stbtt_GetCodepointBitmapBox(&g_font,static_cast<int>(codepoint),factor,factor,&x0,&y0,&x1,&y1);
  out.advance=advance(codepoint,factor);out.left=x0;out.top=y0;
  if(x1>x0&&y1>y0&&x1-x0<=kGlyphLimit&&y1-y0<=kGlyphLimit){out.width=x1-x0;out.height=y1-y0;}
  const auto &old=g_metrics[g_metric_next];
  if(old.size)g_metric_index.erase(old.codepoint,old.size,unsigned(g_metric_next));
  g_metrics[g_metric_next]={codepoint,pixel_size,out};
  if(g_metric_index.needs_rebuild()){
    g_metric_index.clear();
    for(unsigned i=0;i<g_metrics.size();++i)if(g_metrics[i].size&&i!=g_metric_next)
      g_metric_index.insert(g_metrics[i].codepoint,g_metrics[i].size,i);
  }
  g_metric_index.insert(codepoint,pixel_size,unsigned(g_metric_next));
  g_metric_next=(g_metric_next+1)%g_metrics.size();
  return true;
}
bool glyph_metrics(uint32_t codepoint,unsigned pixel_size,GlyphMetrics &out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return glyph_metrics_unlocked(codepoint,pixel_size,out);
}
bool copy_glyph_bitmap(uint32_t codepoint,unsigned pixel_size,uint8_t *out,
                      size_t bytes,size_t stride,bool antialias) {
  std::lock_guard<std::mutex> lock(g_mutex);
  GlyphMetrics metrics;if(!glyph_metrics_unlocked(codepoint,pixel_size,metrics))return false;
  if(!out||stride<static_cast<size_t>(metrics.width)||
      (metrics.height&&stride>bytes/static_cast<size_t>(metrics.height)))return false;
  const uint8_t *coverage=nullptr;
  const auto glyph=glyph_for(codepoint,pixel_size,scale(pixel_size),coverage);
  for(unsigned row=0;row<glyph.height;++row){
    auto *destination=out+row*stride;std::memset(destination,0,stride);
    for(unsigned col=0;col<glyph.width;++col){const auto alpha=coverage[row*glyph.width+col];destination[col]=antialias?alpha:alpha>=128?255:0;}
  }
  return true;
}

TextBounds bounds(const char *utf8,unsigned pixel_size) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_ready || !utf8 || !pixel_size) return {};
  const float glyph_scale=scale(pixel_size);
  const int baseline=static_cast<int>(std::lround(g_ascent*glyph_scale));
  int pen=0,left=0,top=0,right=0,bottom=0;bool any=false;
  for (const char *cursor=utf8;*cursor;) {
    uint32_t cp=next_codepoint(cursor);
    if(cp=='\r'||cp=='\n'||cp=='\t')cp=' ';
    int x0,y0,x1,y1;GlyphMetrics metric;
    const bool cached=glyph_metrics_unlocked(cp,pixel_size,metric);
    if(cached){x0=metric.left;y0=metric.top;x1=x0+metric.width;y1=y0+metric.height;}
    else stbtt_GetCodepointBitmapBox(&g_font,static_cast<int>(cp),glyph_scale,glyph_scale,&x0,&y0,&x1,&y1);
    if(x1>x0 && y1>y0){
      if(!any){left=pen+x0;top=baseline+y0;right=pen+x1;bottom=baseline+y1;any=true;}
      else{left=std::min(left,pen+x0);top=std::min(top,baseline+y0);right=std::max(right,pen+x1);bottom=std::max(bottom,baseline+y1);}
    }
    pen+=cached?metric.advance:advance(cp,glyph_scale);
  }
  return {left,top,right-left,bottom-top};
}

void draw(int x, int y, const char *utf8, unsigned pixel_size,
          Pixel pixel, void *context) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_ready || utf8 == nullptr || pixel == nullptr || pixel_size == 0) return;
  const float glyph_scale = scale(pixel_size);
  const int baseline = y + static_cast<int>(std::lround(g_ascent * glyph_scale));
  for (const char *cursor = utf8; *cursor != '\0';) {
    uint32_t codepoint = next_codepoint(cursor);
    if (codepoint == '\r' || codepoint == '\n' || codepoint == '\t') codepoint = ' ';
    const uint8_t *coverage = nullptr;
    const auto glyph = glyph_for(codepoint, pixel_size, glyph_scale, coverage);
    for (unsigned row = 0; row < glyph.height; ++row)
      for (unsigned column = 0; column < glyph.width; ++column) {
        const uint8_t alpha = coverage[row * glyph.width + column];
        if (alpha) pixel(context, x + glyph.left + static_cast<int>(column),
                         baseline + glyph.top + static_cast<int>(row), alpha);
      }
    x += glyph.advance;
  }
}

}  // namespace lunaredu::lunaros::text_raster
