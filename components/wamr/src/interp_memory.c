/* Interpreter-only adaptation. Executable mappings are deliberately rejected;
 * the application VM does not require changing ESP32-S3 memory protection. */
#include "platform_api_vmcore.h"
#include <esp_heap_caps.h>
void *os_mmap(void *hint, size_t size, int prot, int flags,
              os_file_handle file) {
  (void)hint;
  (void)flags;
  (void)file;
  if (prot & MMAP_PROT_EXEC)
    return NULL;
  return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void os_munmap(void *addr, size_t size) {
  (void)size;
  heap_caps_free(addr);
}
void *os_mremap(void *addr, size_t old_size, size_t new_size) {
  return os_mremap_slow(addr, old_size, new_size);
}
int os_mprotect(void *addr, size_t size, int prot) {
  (void)addr;
  (void)size;
  return prot & MMAP_PROT_EXEC ? -1 : 0;
}
void os_dcache_flush(void) { __asm__ __volatile__("memw" ::: "memory"); }
