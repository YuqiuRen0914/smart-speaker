#include "lunaredu_audio_bridge.h"
#include <cassert>
int main() {
  using namespace lunaredu::audio;
  assert(!setup_local_pcm_audio_bridge());
  assert(!local_pcm_audio_bridge_ready());
  assert(local_pcm_audio_bridge_status().last_error == "disabled");
  assert(!local_pcm_audio_bridge_enable_output(true));
}
