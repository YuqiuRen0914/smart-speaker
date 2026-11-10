#include "lunaredu_lunaros/desktop/local/font_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sys/stat.h>

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_littlefs.h>
#include <esp_log.h>
#include <lvgl.h>

#include "lunaredu_lunaros/desktop/core/utf8.h"
#include "lunaredu_lunaros/delivery/managed_assets.h"
#include "lunaredu_lunaros/persistence/storage_pool.h"

namespace {

constexpr const char *kTag = "lunar_os_font";
constexpr const char *kPartitionLabel = "fonts";
constexpr const char *kMountPath = "/fonts";
constexpr const char *kFontFilePath = "/fonts/lunar-sans-sc.ttf";
constexpr size_t kMinimumFontBytes = 1000000;

// 72 px is reserved for the numeric full-screen clock. Using a real font
// instance keeps LVGL's measured bounds and centering correct; object
// transform scaling does not update a label's layout bounds on hardware.
constexpr std::array<int32_t, 7> kFontSizes = {10, 12, 14, 16, 20, 24, 72};
constexpr std::array<size_t, 7> kGlyphCacheCounts = {24, 24, 32, 40, 24, 16, 12};

std::array<lv_font_t *, kFontSizes.size()> g_fonts = {};
uint8_t *g_font_data = nullptr;
size_t g_font_data_size = 0;
bool g_attempted = false;
bool g_ready = false;
const char *g_failure_reason = "not_attempted";
size_t g_missing_glyph_count = 0;

const lv_font_t *font_or_rescue(size_t index) {
  if (g_ready && index < g_fonts.size() && g_fonts[index] != nullptr) {
    return g_fonts[index];
  }
  return &lv_font_montserrat_16;
}

void destroy_fonts() {
  for (lv_font_t *&font : g_fonts) {
    if (font != nullptr) {
      lv_tiny_ttf_destroy(font);
      font = nullptr;
    }
  }
}

void release_font_data() {
  if (g_font_data != nullptr) {
    heap_caps_free(g_font_data);
    g_font_data = nullptr;
    g_font_data_size = 0;
  }
}

bool load_font_into_psram(size_t font_size) {
  if (lunaredu::lunaros::storage_pool_enabled()) {
    std::string error;
    g_font_data = static_cast<uint8_t *>(lunaredu::lunaros::load_managed_asset("fonts", g_font_data_size, error));
    if (!g_font_data) g_failure_reason = "managed_font_unavailable";
    return g_font_data != nullptr;
  }
  g_font_data = static_cast<uint8_t *>(
      heap_caps_malloc(font_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (g_font_data == nullptr) {
    ESP_LOGE(kTag, "font PSRAM allocation failed: bytes=%u",
             static_cast<unsigned>(font_size));
    g_failure_reason = "psram_allocation_failed";
    return false;
  }

  FILE *font_file = std::fopen(kFontFilePath, "rb");
  if (font_file == nullptr) {
    ESP_LOGE(kTag, "font asset open failed: %s", kFontFilePath);
    g_failure_reason = "asset_open_failed";
    release_font_data();
    return false;
  }

  const size_t bytes_read = std::fread(g_font_data, 1, font_size, font_file);
  const int close_result = std::fclose(font_file);
  if (bytes_read != font_size || close_result != 0) {
    ESP_LOGE(kTag, "font asset read failed: expected=%u actual=%u",
             static_cast<unsigned>(font_size),
             static_cast<unsigned>(bytes_read));
    g_failure_reason = "asset_read_failed";
    release_font_data();
    return false;
  }
  g_font_data_size = font_size;
  return true;
}

bool font_supports_codepoint(void *context, uint32_t codepoint) {
  const auto *font = static_cast<const lv_font_t *>(context);
  if (font == nullptr) {
    return false;
  }
  lv_font_glyph_dsc_t descriptor = {};
  return lv_font_get_glyph_dsc(font, &descriptor, codepoint, 0);
}

} // namespace

extern "C" bool lunaros_font_runtime_setup(void) {
  if (g_attempted) {
    return g_ready;
  }
  g_attempted = true;

  size_t font_size = 0;
  if (!lunaredu::lunaros::storage_pool_enabled()) {
  esp_vfs_littlefs_conf_t config = {};
  config.base_path = kMountPath;
  config.partition_label = kPartitionLabel;
  config.format_if_mount_failed = false;
  config.read_only = true;
  config.dont_mount = false;
  config.grow_on_mount = false;

  const esp_err_t mount_result = esp_vfs_littlefs_register(&config);
  if (mount_result != ESP_OK && mount_result != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "font partition mount failed: %s",
             esp_err_to_name(mount_result));
    g_failure_reason = "partition_mount_failed";
    return false;
  }

  struct stat font_stat = {};
  if (stat(kFontFilePath, &font_stat) != 0 ||
      font_stat.st_size < static_cast<off_t>(kMinimumFontBytes)) {
    ESP_LOGE(kTag, "font asset is missing or invalid: %s", kFontFilePath);
    g_failure_reason = "asset_missing_or_too_small";
    return false;
  }

  font_size = static_cast<size_t>(font_stat.st_size);
  }
  if (!load_font_into_psram(font_size)) {
    return false;
  }

  for (size_t index = 0; index < kFontSizes.size(); ++index) {
    g_fonts[index] = lv_tiny_ttf_create_data_ex(
        g_font_data, g_font_data_size, kFontSizes[index], LV_FONT_KERNING_NONE,
        kGlyphCacheCounts[index]);
    if (g_fonts[index] == nullptr) {
      ESP_LOGE(kTag, "font instance creation failed: size=%ld",
               static_cast<long>(kFontSizes[index]));
      g_failure_reason = "font_instance_create_failed";
      destroy_fonts();
      release_font_data();
      return false;
    }
  }

  g_ready = true;
  g_failure_reason = "";
  ESP_LOGI(kTag,
           "shared font ready: bytes=%u memory=psram sizes=10,12,14,16,20,24,72",
           static_cast<unsigned>(g_font_data_size));
  return true;
}

extern "C" bool lunaros_font_runtime_ready(void) { return g_ready; }

extern "C" const char *lunaros_font_runtime_failure_reason(void) {
  return g_failure_reason;
}

extern "C" size_t lunaros_font_runtime_asset_bytes(void) {
  return g_font_data_size;
}

extern "C" size_t lunaros_font_runtime_missing_glyph_count(void) {
  return g_missing_glyph_count;
}

extern "C" size_t lunaros_font_runtime_sanitize_utf8(
    const lv_font_t *font, const char *input, char *output,
    size_t output_capacity, size_t *replacement_count) {
  size_t replacements = 0;
  const size_t written = lunaredu::desktop::sanitize_utf8(
      input, output, output_capacity, font_supports_codepoint,
      const_cast<lv_font_t *>(font), &replacements);
  if (replacements > 0) {
    const size_t previous = g_missing_glyph_count;
    g_missing_glyph_count += replacements;
    if (previous == 0) {
      ESP_LOGW(kTag,
               "unsupported runtime text glyphs replaced count=%u",
               static_cast<unsigned>(replacements));
    }
  }
  if (replacement_count != nullptr) {
    *replacement_count = replacements;
  }
  return written;
}

extern "C" const lv_font_t *lunaros_font_runtime_10(void) {
  return font_or_rescue(0);
}

extern "C" const lv_font_t *lunaros_font_runtime_12(void) {
  return font_or_rescue(1);
}

extern "C" const lv_font_t *lunaros_font_runtime_14(void) {
  return font_or_rescue(2);
}

extern "C" const lv_font_t *lunaros_font_runtime_16(void) {
  return font_or_rescue(3);
}

extern "C" const lv_font_t *lunaros_font_runtime_20(void) {
  return font_or_rescue(4);
}

extern "C" const lv_font_t *lunaros_font_runtime_24(void) {
  return font_or_rescue(5);
}

extern "C" const lv_font_t *lunaros_font_runtime_72(void) {
  return font_or_rescue(6);
}
