#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lunaredu::lunaros::text_raster {

// The caller owns the trusted font bytes for the lifetime of the renderer.
// Initialize once on the display owner before measuring or drawing text.
bool initialize(const uint8_t *font_data, size_t font_size);
bool ready();
// Zero before a font is ready; changes when the owned font raster changes.
// Cache/storage resets preserve font identity. Reading requires no font lock.
uint32_t font_generation();
struct FontSource { const uint8_t *data=nullptr;size_t size=0;uint32_t generation=0; };
// Immutable trusted bytes, retained by the provider after initialize succeeds.
// Lets render owners use an upstream font backend without copying font data.
FontSource font_source();
constexpr size_t kGlyphCacheByteLimit = 64 * 1024;
constexpr size_t kGlyphCacheEntryLimit = 128;
struct GlyphCacheStats {
  bool enabled;
  size_t capacity_bytes, used_bytes, entries;
  uint32_t hits, misses, rasterizations, evictions;
};
// Optional borrowed cache storage, owned for the entire drawing lifetime by the
// caller. Configure only on the drawing owner, before any concurrent drawing.
// nullptr/0 disables caching; failure leaves the previous configuration intact.
bool configure_glyph_cache(uint8_t *storage, size_t bytes);

struct FontLoader {
  bool (*load)(const uint8_t *&, size_t &, uint8_t *&, size_t &, std::string &);
  void (*discard)();
};
// Product composition supplies the font source. Successful storage is retained
// by the provider; the rasterizer selects no service or allocator.
inline bool setup_font(FontLoader loader, std::string &error) {
  if (ready()) return true;
  if (!loader.load || !loader.discard) { error="font_loader_missing"; return false; }
  const uint8_t *data=nullptr;
  uint8_t *cache=nullptr;
  size_t bytes=0, cache_bytes=kGlyphCacheByteLimit;
  if (!loader.load(data,bytes,cache,cache_bytes,error)) return false;
  if (!initialize(data,bytes)) {
    loader.discard();
    if (error.empty()) error="font_asset_invalid";
    return false;
  }
  (void)configure_glyph_cache(cache,cache?cache_bytes:0);
  error.clear();
  return true;
}
// Owner-only invalidation, preserving cumulative diagnostic counters.
void clear_glyph_cache();
// Individually atomic counters; safe to sample from another task.
GlyphCacheStats glyph_cache_stats();
int width(const char *utf8, unsigned pixel_size);
int line_height(unsigned pixel_size);
int baseline(unsigned pixel_size);
struct GlyphMetrics { int advance=0,left=0,top=0,width=0,height=0; };
// Metrics do not rasterize. Bitmap is copied synchronously into caller storage;
// no cache pointer escapes (eviction/compaction may move the shared cache).
bool glyph_metrics(uint32_t codepoint,unsigned pixel_size,GlyphMetrics &out);
bool copy_glyph_bitmap(uint32_t codepoint,unsigned pixel_size,uint8_t *out,
                      size_t bytes,size_t stride,bool antialias=true);
struct TextBounds { int left=0,top=0,width=0,height=0; };
// Visible glyph box relative to draw(0,0), distinct from font line-box height.
TextBounds bounds(const char *utf8, unsigned pixel_size);

using Pixel = void (*)(void *context, int x, int y, uint8_t alpha);
// y is the top of the line box; clipping and color blending belong to Pixel.
// Operations serialize the shared scratch/cache across display clients.
// Pixel callbacks may inspect baseline/line_height, but must not reenter
// drawing, measuring, cache configuration or bitmap operations.
void draw(int x, int y, const char *utf8, unsigned pixel_size,
          Pixel pixel, void *context);

}  // namespace lunaredu::lunaros::text_raster
