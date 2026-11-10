#include "control/output_control.h"
#include "lunaros_audio_bridge.h"
#include "pcm_backend.h"
#include "lunaros_audio_focus.h"
#include <cassert>
#include <iostream>
using namespace lunaredu;
namespace {
bool worker_exited = false;
bool input_stop_failed = false;
bool backend_allocation_failed = false, hardware_idle = true;
unsigned output_stops = 0;
unsigned destroyed = 0, created = 0;
class Backend : public audio::PcmBackend {
public:
  ~Backend() override { assert(worker_exited); ++destroyed; }
  int input_sample_rate() const override { return 16000; }
  int output_sample_rate() const override { return 16000; }
  bool enable_input(bool value) override { return value || !input_stop_failed; }
  bool enable_output(bool value) override { if (!value) ++output_stops; return true; }
  bool configure_output_sample_rate(uint32_t) override { return true; }
  bool read_input(int16_t *,size_t,uint32_t) override { return true; }
  bool write_output(const int16_t *,size_t,uint16_t,uint32_t) override { return true; }
  bool quiesce(std::string &error) override {
    if(!PcmBackend::quiesce(error))return false;
    if(!hardware_idle){error="audio_foreground_stop_pending";return false;}
    error.clear();return true;
  }
  bool shutdown(std::string &error) override {
    error = worker_exited ? "" : "worker_pending"; return worker_exited;
  }
};
}
namespace lunaredu::audio {
std::unique_ptr<PcmBackend> create_pcm_backend(lunaros::KernelRuntime *,const char *,std::string &error) {
  if(backend_allocation_failed){error="injected_backend_allocation_failure";return nullptr;}
  ++created; return std::make_unique<Backend>();
}
}
int main() {
 lunaredu::lunaros::OutputControl control;
 std::string control_error;
 assert(control.register_provider({"audio", "audio", 0, false}, control_error));
 assert(control.register_consumer({"foreground", "audio", 50}, control_error));
 assert(control.register_consumer({"product", "audio", 100}, control_error));
 assert(control.seal(control_error));
  std::string error;
  backend_allocation_failed=true;
  assert(!audio::start_local_pcm_audio_bridge(control,"audio"));
  audio::AudioFocusLease maintenance;
  assert(audio::local_audio_focus().acquire("product",true,true,maintenance,error));
  assert(audio::local_audio_focus().release(maintenance,error));
  assert(audio::stop_local_pcm_audio_bridge(error));
  backend_allocation_failed=false;
  assert(audio::start_local_pcm_audio_bridge(control,"audio"));
  hardware_idle=false;
  assert(!audio::local_audio_focus().acquire("product",true,true,maintenance,error));
  assert(!maintenance.generation && error=="audio_foreground_stop_pending");
  hardware_idle=true;
  assert(audio::local_audio_focus().acquire("product",true,true,maintenance,error));
  {audio::ResidentAudioAccess access;assert(!access);}
  hardware_idle=false;
  assert(!audio::local_audio_focus().release(maintenance,error));
  assert(maintenance.generation);
  {audio::ResidentAudioAccess access;assert(!access);}
  hardware_idle=true;
  assert(audio::local_audio_focus().release(maintenance,error));
  assert(!control.reset(error));
  assert(!audio::stop_local_pcm_audio_bridge(error) && error == "worker_pending");
  assert(destroyed == 0 && !audio::start_local_pcm_audio_bridge(control,"audio"));
  worker_exited = true;
  assert(audio::stop_local_pcm_audio_bridge(error) && destroyed == 1);
  assert(!control.observe("audio",0).provider_attached);
  assert(audio::start_local_pcm_audio_bridge(control,"audio") && created == 2);
  audio::AudioFocusLease lease;
  assert(audio::local_audio_focus().acquire("foreground",true,true,lease,error));
  assert(!audio::stop_local_pcm_audio_bridge(error) && error == "audio_focus_retirement_required");
  assert(destroyed == 1);
  assert(audio::local_audio_focus().release(lease,error));
  assert(audio::stop_local_pcm_audio_bridge(error) && destroyed == 2);
  assert(audio::stop_local_pcm_audio_bridge(error) && destroyed == 2);
  assert(audio::start_local_pcm_audio_bridge(control,"audio") && created == 3);
  input_stop_failed=true;
  const auto before_output_stops=output_stops;
  assert(!audio::stop_local_pcm_audio_bridge(error) && error=="local_audio_device_stop_failed");
  assert(output_stops==before_output_stops+1 && destroyed==2);
  assert(!control.reset(error));
  input_stop_failed=false;
  assert(audio::stop_local_pcm_audio_bridge(error) && destroyed==3);
  struct Driver {bool fail=true;unsigned input=0,output=0;} driver;
  audio::LocalPcmAudioBridgeDriver ports;
  ports.context=&driver;ports.input_sample_rate=ports.output_sample_rate=16000;
  ports.enable_input=[](void *p,bool){auto &d=*static_cast<Driver *>(p);++d.input;return !d.fail;};
  ports.enable_output=[](void *p,bool){++static_cast<Driver *>(p)->output;return true;};
  ports.configure_output=[](void *,uint32_t){return true;};
  ports.read_input=[](void *,int16_t *,size_t,uint32_t){return true;};
  ports.write_output=[](void *,const int16_t *,size_t,uint16_t,uint32_t){return true;};
  assert(audio::configure_local_pcm_audio_bridge_driver(ports));
  assert(audio::start_local_pcm_audio_bridge(control,"audio"));
  assert(!audio::stop_local_pcm_audio_bridge(error));
  assert(driver.input==1 && driver.output==1 && !audio::configure_local_pcm_audio_bridge_driver(ports));
  driver.fail=false;
  assert(audio::stop_local_pcm_audio_bridge(error));
  assert(driver.input==2 && driver.output==2);
  assert(audio::start_local_pcm_audio_bridge(control,"audio") && created==4);
  assert(audio::stop_local_pcm_audio_bridge(error) && destroyed==4);
  assert(driver.input==2 && driver.output==2); // old borrowed driver was detached
  std::cout << "PASS PCM retirement retains live backend and foreground focus\n";
}
