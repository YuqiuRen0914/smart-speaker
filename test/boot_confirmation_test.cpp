#include "lunaredu_lunaros/delivery/boot_health.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
int main() {
  lunaredu::lunaros::BootHealthGate gate{15000};
  assert(!gate.ready(0, true, false));
  assert(!gate.ready(100, true, true));
  assert(!gate.ready(15099, true, true));
  assert(gate.ready(15100, true, true));
  assert(!gate.ready(16000, true, false));
  assert(!gate.ready(16001, true, true));
  assert(!gate.ready(30000, true, true));
  assert(gate.ready(31001, true, true));
  assert(!gate.ready(32000, true, false));
  assert(!gate.ready(60000, false, true));
  assert(!gate.ready(UINT32_MAX - 100, true, true));
  assert(gate.ready(15000, true, true));
  puts("boot confirmation tests passed");
}
