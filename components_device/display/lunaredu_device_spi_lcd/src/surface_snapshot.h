#pragma once
#include <mutex>
namespace lunaredu::device::detail {
// A diagnostic reader must not wait behind an in-flight DMA transaction.
// It can retry later; drawing and its borrowed source remain owned by the I/O path.
template<class Mutex,class Snapshot,class Capture>
bool try_surface_snapshot(Mutex &mutex,Snapshot &output,Capture capture) {
  std::unique_lock<Mutex> lock(mutex,std::try_to_lock);
  if(!lock.owns_lock())return false;
  output=capture();return true;
}
}
