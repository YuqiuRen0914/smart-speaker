"""Exercise the real BSP mount lifetime with DMA allocation pressure."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    for directory in ['bsp', 'driver']:
        (p / directory).mkdir()
    (p / 'stub.h').write_text(r'''
#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_ERR_INVALID_ARG=1, ESP_ERR_INVALID_STATE=2, ESP_ERR_NO_MEM=3;
constexpr int BSP_RESOURCE_TFCARD=1, SDMMC_FREQ_DEFAULT=20000, GPIO_NUM_NC=-1, SDMMC_SLOT_FLAG_INTERNAL_PULLUP=1;
constexpr int MALLOC_CAP_DMA=1, MALLOC_CAP_INTERNAL=2;
using gpio_num_t=int;
namespace lunaredu::bsp::pins { constexpr int kTfCardClock=1,kTfCardCommand=2,kTfCardData0=3,kTfCardBusWidth=1; }
struct sdmmc_host_t {int max_freq_khz=0; void *dma_aligned_buffer=nullptr;};
struct sdmmc_slot_config_t {int clk,cmd,d0,d1,d2,d3,d4,d5,d6,d7,width,flags;};
struct esp_vfs_fat_sdmmc_mount_config_t {bool format_if_mount_failed;int max_files,allocation_unit_size;};
struct sdmmc_card_t { struct {unsigned capacity=100,sector_size=512;} csd; };
struct bsp_sdcard_status_t {bool mounted;const char *mount_path;uint64_t capacity_bytes;const char *last_error;};
#define SDMMC_HOST_DEFAULT() sdmmc_host_t{}
#define SDMMC_SLOT_CONFIG_DEFAULT() sdmmc_slot_config_t{}
#define VFS_FAT_MOUNT_DEFAULT_CONFIG() esp_vfs_fat_sdmmc_mount_config_t{}
int bsp_resource_acquire(int,const char*);int bsp_resource_release(int,const char*);
const char *esp_err_to_name(int);
void *heap_caps_malloc(size_t,unsigned);void heap_caps_free(void*);
int esp_vfs_fat_sdmmc_mount(const char*,const sdmmc_host_t*,const sdmmc_slot_config_t*,const esp_vfs_fat_sdmmc_mount_config_t*,sdmmc_card_t**);
int esp_vfs_fat_sdcard_unmount(const char*,sdmmc_card_t*);
int bsp_sdcard_mount(const char*);int bsp_sdcard_unmount();bsp_sdcard_status_t bsp_sdcard_get_status();
''')
    for header in ['bsp/lunaredu_bsp.h','driver/sdmmc_host.h','esp_vfs_fat.h','sdmmc_cmd.h','esp_heap_caps.h']:
        (p / header).write_text('#include "stub.h"\n')
    (p / 'test.cpp').write_text(r'''
#include "stub.h"
#include <cassert>
#include <cstdlib>
static bool pressure=false;static int mount_error=0,unmount_error=0,claims=0,allocations=0;
static void *scratch=nullptr;static sdmmc_card_t card;
int bsp_resource_acquire(int,const char*) {++claims;return 0;}
int bsp_resource_release(int,const char*) {--claims;return 0;}
const char *esp_err_to_name(int) {return "failure";}
void *heap_caps_malloc(size_t bytes,unsigned caps) {
 assert(bytes>=512 && (caps & MALLOC_CAP_DMA)); if(pressure) return nullptr;
 ++allocations;return scratch=std::malloc(bytes);
}
void heap_caps_free(void *p) {assert(p==scratch);--allocations;std::free(p);scratch=nullptr;}
int esp_vfs_fat_sdmmc_mount(const char*,const sdmmc_host_t *h,const sdmmc_slot_config_t*,const esp_vfs_fat_sdmmc_mount_config_t*,sdmmc_card_t **out) {
 assert(h->dma_aligned_buffer && h->dma_aligned_buffer==scratch);
 *out=&card;return mount_error;
}
int esp_vfs_fat_sdcard_unmount(const char*,sdmmc_card_t*) {assert(scratch);return unmount_error;}
int main() {
 pressure=true;assert(bsp_sdcard_mount(nullptr)==ESP_ERR_NO_MEM);assert(claims==0 && allocations==0);
 pressure=false;mount_error=4;assert(bsp_sdcard_mount(nullptr)==4);assert(claims==0 && allocations==0);
 mount_error=0;assert(bsp_sdcard_mount(nullptr)==0);assert(claims==1 && allocations==1);
 pressure=true;assert(bsp_sdcard_mount(nullptr)==0);assert(allocations==1);
 assert(bsp_sdcard_get_status().mounted); // Mounted I/O keeps its reserved sector under pressure.
 unmount_error=5;assert(bsp_sdcard_unmount()==5);assert(allocations==1 && claims==1);
 unmount_error=0;assert(bsp_sdcard_unmount()==0);assert(allocations==0 && claims==0);
 assert(bsp_sdcard_unmount()==0);assert(!bsp_sdcard_get_status().mounted);
}
''')
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(p),str(root/'components/lunaredu_bsp/src/bsp_sdcard.cpp'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('SD DMA reservation: pressure, mount failure, reuse and unmount lifetime passed')
