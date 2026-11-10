#pragma once
#include <ArduinoJson.h>
#include "lunaredu_lunaros/desktop/core/view_data.h"
namespace lunaredu::desktop::document_widgets {
void build_file_button(JsonArray, const FileViewEntry &, size_t, const char *, const char *intent);
}
