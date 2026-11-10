#include "lunaros_audio_focus.h"
#include "control/output_control.h"
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
using namespace lunaredu::audio;
bool quiesce(void *, std::string &error) { error.clear(); return true; }
void configure(OutputControl &control, AudioFocusBroker &broker) {
  std::string error;
  assert(control.register_provider({"audio", "audio", 0, false}, error));
  for (auto caller : {"diagnostics", "test", "other", "resume-failure", "rollback-failure",
                      "after-recovery", "failed", "retry"})
    assert(control.register_consumer({caller, "audio", 50}, error));
  assert(control.seal(error));
  assert(broker.configure(control, "audio", {nullptr,quiesce}, error));
}
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
void in_flight_handoff(ResidentAudioLane lane) {
  struct Work {
    OutputControl control;
    AudioFocusBroker broker;
    AudioFocusLease lease;
    std::atomic<bool> done{false};
    bool ok=false;
  } work;
  configure(work.control, work.broker);
#ifdef _WIN32
  HANDLE worker = nullptr;
#else
  std::thread worker;
#endif
  {
    ResidentAudioAccess access(work.broker, lane);
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
    const auto pending = work.control.observe("audio", 0);
    assert(pending.pending.id && !work.control.holds(pending.pending.id, "diagnostics", 0));
    assert(!work.done.load()); // cannot grant while old PCM call is active
  }
#ifdef _WIN32
  assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);
#else
  worker.join();
#endif
  assert(work.done.load() && work.ok);
  { ResidentAudioAccess stale(work.broker);assert(!stale); }
  { ResidentAudioAccess stale(work.broker, ResidentAudioLane::Capture);assert(!stale); }
  std::string error;assert(work.broker.release(work.lease,error));
}
int main() {
  // A blocking microphone read must not serialize the independent speaker
  // lane, while foreground handoff must still drain either admitted lane.
  {
    OutputControl control;
    AudioFocusBroker duplex;
    configure(control, duplex);
    ResidentAudioAccess microphone(duplex, ResidentAudioLane::Capture);
    assert(microphone);
    ResidentAudioAccess speaker(duplex);
    assert(speaker);
  }
  in_flight_handoff(ResidentAudioLane::Capture);
  in_flight_handoff(ResidentAudioLane::Playback);
  OutputControl registry;
  AudioFocusBroker broker;
  configure(registry, broker);
  Resident resident{&broker};
  std::string error;
  assert(broker.attach_resident(transition, &resident, error));
  AudioFocusBroker competing;
  assert(!competing.configure(registry,"audio",{nullptr,quiesce},error) && error=="output_provider_owned");
  assert(!registry.reset(error));
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
  assert(registry.observe("audio", 0).retiring && !registry.holds(first.generation, "resume-failure", 0));
  assert(!broker.acquire("blocked", true, true, second, error));
  { ResidentAudioAccess access(broker); assert(!access); }
  resident.fail_resume = false;
  assert(broker.release(first, error) && !first.generation);
  assert(!resident.paused && !broker.snapshot().recovery_pending);
  resident.fail = resident.fail_resume = true;
  assert(!broker.acquire("rollback-failure", true, true, first, error));
  assert(!first.generation && broker.snapshot().owner.empty());
  assert(broker.snapshot().recovery_pending);
  assert(registry.observe("audio", 0).pending.id);
  assert(!registry.reset(error));
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
  OutputControl control;
  assert(control.register_provider({"audio", "audio_focus", 0, false}, error));
  assert(control.register_consumer({"factory", "audio", 50}, error));
  assert(control.seal(error));
  AudioFocusBroker managed;
  assert(managed.configure(control, "audio", {nullptr,quiesce}, error));
  AudioFocusLease managed_lease;
  assert(!managed.acquire("other", true, true, managed_lease, error));
  assert(error == "output_control_denied");
  assert(managed.acquire("factory", true, true, managed_lease, error));
  assert(control.holds(managed_lease.generation, "factory", 0));
  assert(managed.release(managed_lease, error));
  assert(!control.snapshot("audio", 0).id);
  AudioFocusBroker unbound;
  { ResidentAudioAccess denied(unbound); assert(!denied); }
  assert(!unbound.acquire("factory",true,true,managed_lease,error) && error=="audio_focus_unavailable");
  assert(!unbound.attach_resident(transition,&resident,error));
  bool stopped = false;
  auto stop = [](void *p, std::string &e) { if (!*static_cast<bool *>(p)) { e="worker_pending"; return false; } e.clear(); return true; };
  assert(!broker.stop(stop,&stopped,error));
  assert(broker.snapshot().closing && broker.snapshot().configured);
  { ResidentAudioAccess denied(broker); assert(!denied); }
  assert(!broker.configure(registry,"audio",{nullptr,quiesce},error));
  assert(!broker.acquire("retry",true,true,first,error));
  stopped = true;
  assert(broker.stop(stop,&stopped,error));
  assert(!broker.snapshot().configured);
  assert(broker.configure(registry,"audio",{nullptr,quiesce},error));
  assert(broker.attach_resident(transition,&resident,error));
  assert(broker.detach_resident(transition,&resident,error));
  assert(broker.acquire("retry",true,true,first,error));
  assert(broker.release(first,error));
  OutputControl physical_control;
  AudioFocusBroker physical;
  assert(physical_control.register_provider({"device","audio",0,false},error));
  assert(physical_control.register_consumer({"foreground","device",50},error));
  assert(physical_control.seal(error));
  assert(!physical.configure(physical_control,"device",{},error));
  bool device_idle=false;
  assert(physical.configure(physical_control,"device",{&device_idle,
    [](void *p,std::string &e) {if(!*static_cast<bool *>(p)){e="audio_foreground_stop_pending";return false;}e.clear();return true;}},error));
  AudioFocusLease physical_lease;
  assert(!physical.acquire("foreground",false,true,physical_lease,error));
  assert(!physical_lease.generation && !physical_control.observe("device",0).pending.id);
  device_idle=true;
  assert(physical.acquire("foreground",false,true,physical_lease,error));
  device_idle=false;
  assert(!physical.release(physical_lease,error));
  assert(physical_lease.generation && physical_control.observe("device",0).retiring);
  assert(!physical_control.holds(physical_lease.generation,"foreground",0));
  assert(!physical.stop(stop,&stopped,error));
  device_idle=true;
  assert(physical.release(physical_lease,error));
  assert(physical.stop(stop,&stopped,error));
  assert(physical_control.reset(error));
  std::cout << "audio focus tests passed\n";
}
