#pragma once

/* Private BSP contract: no allocation, RTOS, driver or Device dependency. */
enum lunaros_startup_pull {
    LUNAROS_STARTUP_PULL_NONE,
    LUNAROS_STARTUP_PULL_UP,
    LUNAROS_STARTUP_PULL_DOWN,
};

void lunaros_startup_apply_safe_outputs(void);
