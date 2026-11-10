#include "lunaredu_lunaros/persistence/system_audio.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include <cassert>
#include <string>
namespace {
 std::string saved = "invalid"; bool persistence_ok = true; bool driver_ok = true;
 int writes = 0; unsigned applied = 0;
 bool output(uint8_t value) { ++writes; applied = value; return driver_ok; }
}
namespace lunaredu::lunaros {
 std::string settings_get_string(const char *ns,const char *key,const char *) {
  assert(std::string(ns)=="lunar_os" && std::string(key)=="speaker_volume");return saved;
 }
 bool settings_set_string(const char *,const char *,const std::string &value) {saved=value;return persistence_ok;}
}
int main() {
 using namespace lunaredu::lunaros;
 configure_audio_output({output});
 assert(system_volume_percent()==70);
 assert(apply_system_volume() && applied==70);
 assert(!set_system_volume_percent(101) && system_volume_percent()==70);
 assert(set_system_volume_percent(25) && applied==25 && saved=="25");
 driver_ok=false;
 assert(!set_system_volume_percent(40) && system_volume_percent()==40);
 driver_ok=true;assert(apply_system_volume() && applied==40);
 persistence_ok=false;assert(!set_system_volume_percent(0) && applied==0);
 configure_audio_output({});assert(!apply_system_volume());
 configure_audio_output({output});assert(apply_system_volume() && applied==0);
}
