#pragma once
#include "app/execution/draw_list.h"
#include "app/lifecycle/application.h"
#include <memory>
#include <string>
namespace lunaredu::lunaros::apps {
// Borrowed foreground operations. Desktop knows no installed catalog, VM,
// filesystem or concrete Runtime owner. The provider outlives its consumers.
struct ApplicationForegroundPorts {
  void *context = nullptr;
  // Repeated calls for one identity observe the same startup request. Pending
  // must return promptly; close retains contexts until retirement is confirmed.
  app::ApplicationStartResult (*open)(void *, const std::string &, std::string &) = nullptr;
  bool (*close)(void *, std::string &) = nullptr;
  bool (*input)(void *, int, int, int, int) = nullptr;
  bool (*running)(void *) = nullptr;
  bool (*realtime)(void *) = nullptr;
  std::shared_ptr<const DrawList> (*frame)(void *) = nullptr;
  bool valid() const {
    return context && open && close && input && running && realtime && frame;
  }
};
}
