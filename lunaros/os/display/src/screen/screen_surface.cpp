#include "display/screen_surface.h"
namespace lunaredu::display {
ScreenSurface::~ScreenSurface() = default;
bool ScreenSurface::prepare(ScreenBackend backend,std::string &error,ScreenMemory memory,ScreenStorageBudget budget) {
  return session_.prepare(backend,error,memory,0,budget,0,1);
}
bool ScreenSurface::enable_worker(lunaros::KernelRuntime &runtime,lunaros::ManagedTaskDefinition task,std::string &error){return session_.enable_worker(runtime,task,error);}
void ScreenSurface::release_unstarted(){session_.release_unstarted();}
bool ScreenSurface::stop(std::string &error){return session_.stop(error);}
bool ScreenSurface::acquire(const char *owner,std::string &error){return session_.acquire(owner,error);}
bool ScreenSurface::release(std::string &error){return session_.release(error);}
bool ScreenSurface::wait_all(std::string &error){return session_.wait_all(error);}
FrameCanvas ScreenSurface::begin_frame(std::string &error){return session_.begin_frame(error);}
bool ScreenSurface::present(const ScreenRegion *regions,size_t count,const FrameCanvas &frame,std::string &error){return session_.present(frame,regions,count,error);}
bool ScreenSurface::ready() const{return session_.ready();}
uint16_t ScreenSurface::width() const{return session_.width();}
uint16_t ScreenSurface::height() const{return session_.height();}
ScreenCapabilities ScreenSurface::capabilities() const{return session_.capabilities();}
bool ScreenSurface::asynchronous() const{return session_.asynchronous();}
bool ScreenSurface::worker_failed() const{return session_.worker_failed();}
bool ScreenSurface::active() const{return session_.active();}
bool ScreenSurface::retained() const{return session_.retained();}
ScreenMemory ScreenSurface::memory() const{return session_.memory_;}
bool ScreenSurface::holds_frame(const FrameCanvas &frame) const{return session_.holds_frame(frame);}
uint64_t ScreenSurface::ownership_generation() const{return session_.ownership_generation_;}
void ScreenSurface::retain_overlay(){session_.retain_overlay();}
void ScreenSurface::release_overlay_binding(){session_.release_overlay_binding();}
bool ScreenSurface::reserve_overlay(size_t bytes,std::string &error){return session_.reserve_overlay(bytes,error);}
void ScreenSurface::release_overlay(size_t bytes){session_.release_overlay(bytes);}
uint16_t *ScreenSurface::frame(unsigned index) const{return session_.frame(index);}
}
