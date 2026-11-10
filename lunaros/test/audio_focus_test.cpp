#include "lunaredu_lunaros/app/audio_focus.h"
#include <cassert>
#include <iostream>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#else
#include <thread>
#include <chrono>
#endif
using namespace lunaredu::lunaros;
struct Resident {
  AudioFocusBroker *broker;
  bool fail = false;
  bool paused = false;
  int changes = 0;
  bool fail_resume = false;
};
bool transition(void *context, bool paused, std::string &error) {
  auto &r = *static_cast<Resident *>(context);
  ++r.changes;
  assert(paused ? !r.broker->snapshot().owner.empty()
                : r.broker->snapshot().recovery_pending);
  { ResidentAudioAccess access(*r.broker); assert(!access); }
  // Reentrant mutation fails promptly, while observation is always safe.
  AudioFocusLease nested;
  assert(!r.broker->acquire("nested", true, false, nested, error));
  if (!paused && r.broker->snapshot().generation) {
    AudioFocusLease retry{r.broker->snapshot().generation};
    const auto generation=retry.generation;
    assert(!r.broker->release(retry,error) && error=="audio_focus_transition_busy");
    assert(retry.generation==generation);
  }
  if (paused && r.fail) { error = "quiesce_failed"; return false; }
  if (!paused && r.fail_resume) { error = "resume_failed"; return false; }
  r.paused = paused;
  return true;
}
void in_flight_handoff() {
  struct Work {
    AudioFocusBroker broker;
    AudioFocusLease lease;
    std::atomic<bool> done{false};
    bool ok=false;
  } work;
#ifdef _WIN32
  HANDLE worker = nullptr;
#else
  std::thread worker;
#endif
  {
    ResidentAudioAccess access(work.broker);
    assert(access);
    auto run = [](void *context) {
      auto &work = *static_cast<Work *>(context);
      std::string error;
      work.ok = work.broker.acquire("diagnostics",true,true,work.lease,error);
      work.done.store(true);
    };
#ifdef _WIN32
    struct Start { decltype(run) function; Work *work; } start{run,&work};
    worker = CreateThread(nullptr,0,[](LPVOID opaque)->DWORD {
      auto *start = static_cast<Start *>(opaque);
      auto function=start->function; auto *work=start->work; function(work);return 0;
    },&start,0,nullptr);
    assert(worker);
#else
    worker = std::thread(run,&work);
#endif
    for(int i=0;i<1000 && work.broker.snapshot().owner.empty();++i) {
#ifdef _WIN32
      Sleep(1);
#else
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    }
    assert(work.broker.snapshot().owner == "diagnostics");
    assert(!work.done.load()); // cannot grant while old PCM call is active
  }
#ifdef _WIN32
  assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);
#else
  worker.join();
#endif
  assert(work.done.load() && work.ok);
  { ResidentAudioAccess stale(work.broker);assert(!stale); }
  std::string error;assert(work.broker.release(work.lease,error));
}
int main() {
  in_flight_handoff();
  AudioFocusBroker broker;
  Resident resident{&broker};
  std::string error;
  assert(broker.bind_resident(transition, &resident, error));
  AudioFocusLease first, second;
  assert(!broker.acquire("", true, true, first, error));
  assert(!broker.acquire("test", false, false, first, error));
  assert(broker.acquire("test", true, false, first, error));
  { ResidentAudioAccess access(broker); assert(!access); }
  assert(resident.paused && broker.snapshot().microphone && !broker.snapshot().speaker);
  assert(!broker.acquire("other", false, true, second, error));
  auto stale = first;
  assert(broker.release(first, error));
  assert(broker.acquire("resume-failure", true, true, first, error));
  resident.fail_resume = true;
  assert(!broker.release(first, error) && error == "resume_failed");
  assert(first.generation && resident.paused && broker.snapshot().recovery_pending);
  assert(!broker.acquire("blocked", true, true, second, error));
  { ResidentAudioAccess access(broker); assert(!access); }
  resident.fail_resume = false;
  assert(broker.release(first, error) && !first.generation);
  assert(!resident.paused && !broker.snapshot().recovery_pending);
  resident.fail = resident.fail_resume = true;
  assert(!broker.acquire("rollback-failure", true, true, first, error));
  assert(!first.generation && broker.snapshot().owner.empty());
  assert(broker.snapshot().recovery_pending);
  { ResidentAudioAccess access(broker); assert(!access); }
  assert(!broker.recover_resident(error) && error == "resume_failed");
  resident.fail = resident.fail_resume = false;
  assert(broker.acquire("after-recovery", true, true, first, error));
  assert(!broker.snapshot().recovery_pending);
  assert(broker.release(first, error));
  assert(!resident.paused && !first.generation);
  { ResidentAudioAccess access(broker); assert(access); }
  assert(broker.acquire("other", false, true, second, error));
  assert(!broker.release(stale, error));
  assert(broker.snapshot().owner == "other");
  assert(broker.release(second, error));
  resident.fail = true;
  assert(!broker.acquire("failed", true, true, first, error));
  assert(error == "quiesce_failed" && !first.generation);
  assert(broker.snapshot().owner.empty() && !resident.paused);
  resident.fail = false;
  assert(broker.acquire("retry", true, true, first, error));
  assert(broker.release(first, error));
  std::cout << "audio focus tests passed\n";
}
