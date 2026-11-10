#pragma once
#include "lunaros_app_manager_view.h"
namespace lunaredu::lunaros::apps::manager {
void reconcile_selection(const StoreSnapshot &, View &);
std::string view_key(const StoreSnapshot &, const View &);
}
