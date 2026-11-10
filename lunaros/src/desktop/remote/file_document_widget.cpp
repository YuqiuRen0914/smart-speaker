#include "lunaredu_lunaros/desktop/remote/file_document_widget.h"
#include "lunaredu_lunaros/desktop/remote/document_widgets.h"
namespace lunaredu::desktop::document_widgets {
const char *file_icon(lunaredu::desktop::FileKind kind) {
  using lunaredu::desktop::FileKind;
  switch (kind) {
  case FileKind::Folder:
    return "folder";
  case FileKind::Python:
    return "code";
  case FileKind::Performance:
    return "sparkles";
  case FileKind::Other:
    return "file";
  }
  return "file";
}



void build_file_button(JsonArray children,
                       const lunaredu::desktop::FileViewEntry &entry,
                       size_t index, const char *key_prefix,
                       const char *intent) {
  JsonObject button = add_button(
      children, std::string(key_prefix) + "." + std::to_string(index), intent,
      entry.name.c_str());
  button["disabled"] = intent[0] == '\0';
  button["intent"]["payload"]["path"] = entry.path;
  style_card(button);
  style_row(button, 8);
  button["style"]["width"] = "fill";
  button["style"]["height"] = 38;
  JsonArray row = button["children"].as<JsonArray>();
  add_icon(row, std::string(key_prefix) + ".icon." + std::to_string(index),
           file_icon(entry.kind), 16);
  JsonObject text = add_container(
      row, std::string(key_prefix) + ".text." + std::to_string(index));
  text["style"]["grow"] = 1;
  JsonArray text_children = text["children"].as<JsonArray>();
  add_text(text_children,
           std::string(key_prefix) + ".name." + std::to_string(index),
           entry.name, 11, 600);
  if (!entry.detail.empty()) {
    add_text(text_children,
             std::string(key_prefix) + ".detail." + std::to_string(index),
             entry.detail, 9, 400, kColorMuted);
  }
}

}
