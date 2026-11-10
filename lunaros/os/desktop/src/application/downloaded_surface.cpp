#include "desktop/application/downloaded_surface.h"

namespace lunaredu::lunaros::apps {
app::ApplicationStartResult DownloadedSurface::open(const std::string &id, std::string &error) {
    if (!ports_.valid()) { error = "downloaded_foreground_unavailable"; return app::ApplicationStartResult::Failed; }
    if ((starting_ && id != starting_id_) || (!starting_ && running())) {
      error = "downloaded_surface_in_use"; return app::ApplicationStartResult::Failed;
    }
    const auto result=ports_.open(ports_.context,id,error);
    starting_=result==app::ApplicationStartResult::Pending;
    if(starting_){starting_id_=id;return result;}
    if(result==app::ApplicationStartResult::Failed)return result;
    starting_id_.clear();input_.reset(); frame_ = ports_.frame(ports_.context); error.clear(); return app::ApplicationStartResult::Started;
  }

bool DownloadedSurface::close(std::string &error) {
    if (!ports_.valid()) { error = "downloaded_foreground_unavailable"; return false; }
    if (!cancel_input()) { error = "downloaded_input_cancel_pending"; return false; }
    if (!ports_.close(ports_.context, error)) return false;
    starting_=false;starting_id_.clear();input_.reset(); frame_.reset(); return true;
  }

bool DownloadedSurface::cancel_input() {
    // A naturally completed backend has no input recipient. Its resource
    // retirement still goes through close(), including checked failures.
    if (!running()) { input_.reset(); return true; }
    return input_.cancel(&ports_, sink);
  }
}
