#include "lunaros_audio_volume.h"
#include "configuration/settings.h"
#include <cassert>
#include <string>
namespace {
 std::string saved = "invalid"; bool persistence_ok = true; bool driver_ok = true;
 int writes = 0; unsigned applied = 0;
 bool output(uint8_t value) { ++writes; applied = value; return driver_ok; }
}
namespace lunaredu::lunaros {
 SettingsReadResult settings_read_string(const char *ns, const char *key,
                                        size_t maximum, std::string &value,
                                        std::string &error) {
  (void)ns; (void)key;
  value.clear(); error.clear();
  value = saved;
  if (value.empty()) return SettingsReadResult::Missing;
  if (value.size() > maximum) { value.clear(); error = "oversized"; return SettingsReadResult::Failed; }
  return SettingsReadResult::Found;
}
 bool settings_set_string(const char *,const char *,const std::string &value) {saved=value;return persistence_ok;}
}
int main() {
 using namespace lunaredu::audio;
 configure_volume_output({output,35});
 assert(volume_percent()==35);
 assert(apply_volume() && applied==35);
 assert(!set_volume_percent(101) && volume_percent()==35);
 assert(set_volume_percent(25) && applied==25 && saved=="25");
 driver_ok=false;
 assert(!set_volume_percent(40) && volume_percent()==40);
 driver_ok=true;assert(apply_volume() && applied==40);
 persistence_ok=false;assert(!set_volume_percent(0) && applied==0);
 configure_volume_output({});assert(!apply_volume());
 configure_volume_output({output});assert(apply_volume() && applied==0);
 persistence_ok=true;configure_volume_output({});
 bool applied_now=true;
 assert(save_volume_percent(30,applied_now)&&!applied_now&&saved=="30");
 configure_volume_output({output});assert(apply_volume()&&applied==30);
 persistence_ok=false;assert(!save_volume_percent(20,applied_now));
}
