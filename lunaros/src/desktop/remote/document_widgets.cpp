#include "lunaredu_lunaros/desktop/remote/document_widgets.h"
namespace lunaredu::desktop::document_widgets {
void fill_size(JsonObject style, const char *width, const char *height) {
  if (width != nullptr) {
    style["width"] = width;
  }
  if (height != nullptr) {
    style["height"] = height;
  }
}

JsonObject add_container(JsonArray children, const std::string &key) {
  JsonObject node = children.add<JsonObject>();
  node["key"] = key;
  node["type"] = "container";
  node["children"].to<JsonArray>();
  return node;
}

JsonObject add_text(JsonArray children, const std::string &key,
                    const std::string &text, int size, int weight,
                    const char *color) {
  JsonObject node = children.add<JsonObject>();
  node["key"] = key;
  node["type"] = "text";
  node["text"] = text;
  JsonObject text_style = node["textStyle"].to<JsonObject>();
  text_style["size"] = size;
  text_style["weight"] = weight;
  text_style["maxLines"] = 2;
  if (color != nullptr) {
    node["style"]["foreground"] = color;
  }
  return node;
}

JsonObject add_icon(JsonArray children, const std::string &key,
                    const char *name, int size) {
  JsonObject node = children.add<JsonObject>();
  node["key"] = key;
  node["type"] = "icon";
  node["name"] = name;
  node["size"] = size;
  return node;
}

JsonObject add_button(JsonArray children, const std::string &key,
                      const char *intent_type,
                      const char *accessibility_label) {
  JsonObject node = children.add<JsonObject>();
  node["key"] = key;
  node["type"] = "button";
  node["accessibilityLabel"] = accessibility_label;
  node["intent"]["type"] = intent_type;
  node["children"].to<JsonArray>();
  return node;
}

void style_row(JsonObject node, int gap) {
  JsonObject style = node["style"].to<JsonObject>();
  style["direction"] = "row";
  style["align"] = "center";
  style["gap"] = gap;
}

void style_card(JsonObject node, int radius) {
  JsonObject style = node["style"].to<JsonObject>();
  style["background"] = kColorSurface;
  style["borderWidth"] = 0;
  style["radius"] = radius;
  style["padding"] = 8;
}

void build_page_header(JsonArray children, const std::string &key,
                       const char *title, const std::string &detail) {
  JsonObject header = add_container(children, key + ".header");
  style_row(header, 8);
  header["style"]["width"] = "fill";
  header["style"]["height"] = 38;
  JsonArray row = header["children"].as<JsonArray>();
  JsonObject back = add_button(row, key + ".back", "navigation.back", "返回");
  back["style"]["width"] = 36;
  back["style"]["height"] = 36;
  back["style"]["radius"] = 12;
  JsonArray back_children = back["children"].as<JsonArray>();
  add_icon(back_children, key + ".back.icon", "back", 16);
  JsonObject labels = add_container(row, key + ".labels");
  labels["style"]["grow"] = 1;
  JsonArray label_children = labels["children"].as<JsonArray>();
  add_text(label_children, key + ".title", title, 16, 700);
  if (!detail.empty()) {
    add_text(label_children, key + ".detail", detail, 10, 400, kColorMuted);
  }
}

}
