#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Installed by the single VM owner before mp_embed_init. Hooks and context
// must remain valid until the VM is deinitialised; no concurrent replacement.
typedef struct {
    void *context;
    int (*take_abort_request)(void *context);
    void (*yield)(void *context);
    void (*stdout_write)(void *context, const char *text, size_t length);
} MicroPythonEngineHooks;

void micropython_engine_set_hooks(const MicroPythonEngineHooks *hooks);
int micropython_engine_vm_checkpoint(uint32_t *branch_count);
void micropython_engine_stdout_write(const char *text, size_t length);

#ifdef __cplusplus
}
#endif
