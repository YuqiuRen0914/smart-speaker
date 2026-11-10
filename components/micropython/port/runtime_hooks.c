#include "micropython_engine_hooks.h"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#ifndef CONFIG_LUNAREDU_MICROPYTHON_VM_YIELD_BRANCH_INTERVAL
#define CONFIG_LUNAREDU_MICROPYTHON_VM_YIELD_BRANCH_INTERVAL 128
#endif

static MicroPythonEngineHooks owner_hooks;

void micropython_engine_set_hooks(const MicroPythonEngineHooks *hooks) {
    if (hooks != NULL) owner_hooks = *hooks;
    else memset(&owner_hooks, 0, sizeof(owner_hooks));
}

int micropython_engine_vm_checkpoint(uint32_t *branch_count) {
    const int abort_requested = owner_hooks.take_abort_request != NULL
        ? owner_hooks.take_abort_request(owner_hooks.context) : 0;
    if (++*branch_count >= CONFIG_LUNAREDU_MICROPYTHON_VM_YIELD_BRANCH_INTERVAL) {
        *branch_count = 0;
        if (owner_hooks.yield != NULL) owner_hooks.yield(owner_hooks.context);
    }
    return abort_requested;
}

void micropython_engine_stdout_write(const char *text, size_t length) {
    if (owner_hooks.stdout_write != NULL)
        owner_hooks.stdout_write(owner_hooks.context, text, length);
    else if (text != NULL && length != 0)
        (void)fwrite(text, 1, length, stdout);
}

// Replace the embed port's printf implementation through source selection.
// The engine knows only the callback, never the OS log ring or transport.
void mp_hal_stdout_tx_strn_cooked(const char *text, size_t length) {
    micropython_engine_stdout_write(text, length);
}
