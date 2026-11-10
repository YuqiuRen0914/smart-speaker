#include "platform_api_vmcore.h"
korp_tid os_self_thread(void) {
    return (korp_tid)(uintptr_t)xTaskGetCurrentTaskHandle();
}
