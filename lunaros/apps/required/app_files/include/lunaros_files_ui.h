#pragma once
#include "lunaros_files_application.h"
#include "desktop/ui/entry.h"
namespace lunaredu::files {
inline desktop::UiEntry ui_entry() { return {desktop::kShellView,desktop::UiEntryKind::Tile,"os.files"}; }
template<class Bindings>
typename Bindings::Definition ui_binding() { return desktop::entry_ui_binding<Bindings,ui_entry>(kApplicationId); }

}
