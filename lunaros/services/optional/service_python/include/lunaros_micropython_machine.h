#pragma once
#include <cstddef>
#include <cstdint>

namespace lunaredu::micropython {
// Functional lesson I/O, executed on the VM owner. A product supplies actual
// GPIO/ADC/PWM/audio providers and rejects pins occupied by its other services.
// Buffers are borrowed for this call only; no Python object may escape.
struct MachinePort {
  void *context=nullptr;
  bool (*call)(void *,int,const double *,size_t,const uint8_t *,size_t,
               double &,const char *&)=nullptr;
  // Idempotent; stop DMA/PWM and return only resources owned by this session.
  void (*release)(void *)=nullptr;
};
void setup_micropython_lesson_modules(bool machine);
const MachinePort *runtime_machine_port();
void release_micropython_machine_outputs();
}
