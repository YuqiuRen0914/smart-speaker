#include "port/micropython_embed.h"
#include "py/stackctrl.h"
#include "py/qstr.h"
#include "micropython_stack_config.h"
#include "micropython_project.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#ifndef CONFIG_LUNAREDU_MICROPYTHON_TASK_STACK_BYTES
#define CONFIG_LUNAREDU_MICROPYTHON_TASK_STACK_BYTES 12288
#endif
// Preserve the vendored initializer and establish the native stack limit for
// every embedding path, including scripts, REPL and application sessions.
void lunaros_mp_embed_init_unchecked(void *, size_t, void *);
qstr lunaros_mp_recursion_qstr;
void mp_embed_init(void *heap, size_t size, void *stack_top) {
    lunaros_mp_embed_init_unchecked(heap, size, stack_top);
    mp_stack_set_limit(CONFIG_LUNAREDU_MICROPYTHON_TASK_STACK_BYTES - 2048);
    lunaros_mp_recursion_qstr = qstr_from_str("maximum recursion depth exceeded");
    micropython_project_init();
}
