#pragma once
#include <ArduinoJson.h>
#include <string>
namespace lunaredu::desktop::document_widgets {
inline constexpr const char *kColorBackground = "#050507";
inline constexpr const char *kColorSurface = "#111216";
inline constexpr const char *kColorSurfaceMuted = "#23252C";
inline constexpr const char *kColorBorder = "#3A3D46";
inline constexpr const char *kColorText = "#F7F7FA";
inline constexpr const char *kColorMuted = "#989CA7";
inline constexpr const char *kColorPrimary = "#7C8CFF";
inline constexpr const char *kColorDanger = "#F06A74";
inline constexpr const char *kColorWarning = "#F3B955";

void fill_size(JsonObject style, const char *width, const char *height);
JsonObject add_container(JsonArray children, const std::string &key);
JsonObject add_text(JsonArray children, const std::string &key,
                    const std::string &text, int size, int weight = 400,
                    const char *color = nullptr);
JsonObject add_icon(JsonArray children, const std::string &key,
                    const char *name, int size = 18);
JsonObject add_button(JsonArray children, const std::string &key,
                      const char *intent_type,
                      const char *accessibility_label);
void style_row(JsonObject node, int gap = 6);
void style_card(JsonObject node, int radius = 14);
void build_page_header(JsonArray children, const std::string &key,
                       const char *title, const std::string &detail = {});
}
