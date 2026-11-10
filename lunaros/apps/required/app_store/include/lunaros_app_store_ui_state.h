#pragma once
#include "lunaros_app_manager_view.h"
#include "app/execution/draw_list.h"
#include <memory>
namespace lunaredu::lunaros::apps {
struct PackageUiState {
  std::shared_ptr<const StoreSnapshot> store;
  std::shared_ptr<const DrawList> app_frame;
  manager::View store_ui;
  std::string store_view_key;
};
}
