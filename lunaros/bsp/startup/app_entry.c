#include "safe_outputs.h"

void __real_app_main(void);

void __wrap_app_main(void) {
    lunaros_startup_apply_safe_outputs();
    __real_app_main();
}
