#include "control/output_control.h"
#include "lunaros_audio_bridge.h"
#include <cassert>
int main() {
 lunaredu::lunaros::OutputControl control;
 std::string control_error;
 assert(control.register_provider({"audio", "audio", 0, false}, control_error));
 assert(control.register_consumer({"foreground", "audio", 50}, control_error));
 assert(control.seal(control_error));
  using namespace lunaredu::audio;
  assert(!start_local_pcm_audio_bridge(control,"audio"));
  assert(!local_pcm_audio_bridge_ready());
  assert(local_pcm_audio_bridge_status().last_error == "disabled");
  assert(!local_pcm_audio_bridge_enable_output(true));
}
