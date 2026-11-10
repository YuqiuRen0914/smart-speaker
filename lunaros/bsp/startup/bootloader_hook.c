#include "safe_outputs.h"
#include "sdkconfig.h"
#if CONFIG_LUNAROS_PRODUCTION_PREPROVISIONED
#include "esp_secure_boot.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_rom_sys.h"
#endif

/* ESP-IDF forces this symbol into the bootloader to retain the hook. */
void bootloader_hooks_include(void) {}

void bootloader_after_init(void) {
    lunaros_startup_apply_safe_outputs();
#if CONFIG_LUNAROS_PRODUCTION_PREPROVISIONED
    /* IDF's REQUIRE_ALREADY_ENABLED Flash option only exists in development
     * mode. The documented hook runs before bootloader_utility can provision
     * security features. Production is provisioned offline, never on boot. */
    uint8_t crypt_count=0;
    if (!esp_secure_boot_enabled() || !esp_efuse_is_flash_encryption_enabled() ||
        esp_efuse_read_field_blob(ESP_EFUSE_SPI_BOOT_CRYPT_CNT,&crypt_count,3)!=ESP_OK || crypt_count!=7) {
        esp_rom_printf("LunarOS: factory security provisioning required\n");
        for (;;) { /* Fail closed; the boot watchdog may reset, no eFuse write. */ }
    }
#endif
}
