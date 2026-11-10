#include "safe_outputs.h"
#include "lunaros_startup_outputs.h"
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "soc/gpio_sig_map.h"

static inline void apply_output(int pin, int level, enum lunaros_startup_pull pull) {
    /* Preload the latch before selecting GPIO and enabling output. No GPIO
     * hold is used: the runtime driver must be able to take ownership later. */
    gpio_ll_set_level(&GPIO, pin, level);
    esp_rom_gpio_pad_select_gpio(pin);
    gpio_ll_pullup_dis(&GPIO, pin);
    gpio_ll_pulldown_dis(&GPIO, pin);
    if (pull == LUNAROS_STARTUP_PULL_UP) gpio_ll_pullup_en(&GPIO, pin);
    if (pull == LUNAROS_STARTUP_PULL_DOWN) gpio_ll_pulldown_en(&GPIO, pin);
    esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
    gpio_ll_od_disable(&GPIO, pin);
    gpio_ll_input_disable(&GPIO, pin);
    gpio_ll_intr_disable(&GPIO, pin);
    gpio_ll_output_enable(&GPIO, pin);
}

void lunaros_startup_apply_safe_outputs(void) {
#define APPLY(pin, level, pull) apply_output(pin, level, pull);
    LUNAROS_STARTUP_SAFE_OUTPUTS(APPLY)
#undef APPLY
}
