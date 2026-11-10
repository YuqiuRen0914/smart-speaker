#pragma once
#include "desktop/composition/native_entry.h"
#include "lunaredu_pet_application.h"

namespace lunaredu::pet {
struct DesktopDescription {
  static constexpr const char *id = kApplicationId;
  static app::ViewRoute route() { return {id, 0, "pet"}; }
};
// The product registers application() and connects its render ports. Desktop
// exposes this App's own metadata without duplicating its foreground owner.
struct DesktopEntry : lunaros::NativeDesktopEntry<DesktopDescription> {
  static auto tile() {
    return desktop::DesktopApplication{DesktopDescription::id, "宠物",
      desktop::DesktopIcon::Pet, 0xe6b3};
  }
};
}
