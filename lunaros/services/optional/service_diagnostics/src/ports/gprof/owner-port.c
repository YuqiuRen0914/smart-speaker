#include "lunar_gprof_port.h"
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <esp_timer.h>
static bool accepting;
static uint32_t inflight,overflow;
static uint32_t deadline;
static uint8_t *storage;
static const esp_partition_t virtual_partition={.size=65536};
bool IRAM_ATTR lunar_gprof_enter(void){
 __atomic_add_fetch(&inflight,1,__ATOMIC_ACQUIRE);
 if(!__atomic_load_n(&accepting,__ATOMIC_ACQUIRE)){__atomic_sub_fetch(&inflight,1,__ATOMIC_RELEASE);return false;}
 return true;
}
void IRAM_ATTR lunar_gprof_leave(void){__atomic_sub_fetch(&inflight,1,__ATOMIC_RELEASE);}
void IRAM_ATTR lunar_gprof_overflow(void){__atomic_store_n(&overflow,1,__ATOMIC_RELAXED);}
void lunar_gprof_set_active(bool active){__atomic_store_n(&accepting,active,__ATOMIC_RELEASE);}
void lunar_gprof_set_deadline(uint32_t value){__atomic_store_n(&deadline,value,__ATOMIC_RELEASE);}
bool IRAM_ATTR lunar_gprof_expired(void){
 const uint32_t end=__atomic_load_n(&deadline,__ATOMIC_ACQUIRE);
 if(end && (int32_t)((uint32_t)(esp_timer_get_time()/1000)-end)>=0){__atomic_store_n(&accepting,false,__ATOMIC_RELEASE);return true;}
 return false;
}
uint32_t lunar_gprof_inflight(void){return __atomic_load_n(&inflight,__ATOMIC_ACQUIRE);}
bool lunar_gprof_overflowed(void){return __atomic_load_n(&overflow,__ATOMIC_RELAXED)!=0;}
const esp_partition_t *lunar_gprof_store_begin(void){
 if(storage)return NULL;
 storage=heap_caps_calloc(1,virtual_partition.size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 __atomic_store_n(&overflow,0,__ATOMIC_RELAXED);
 return storage?&virtual_partition:NULL;
}
esp_err_t lunar_gprof_store_write(size_t offset,const void *bytes,size_t size){
 if(!storage||!bytes||offset>virtual_partition.size||size>virtual_partition.size-offset)return ESP_ERR_INVALID_SIZE;
 memcpy(storage+offset,bytes,size);return ESP_OK;
}
const uint8_t *lunar_gprof_store_bytes(size_t *size){
 if(!storage||!size)return NULL;
 uint32_t saved=0;memcpy(&saved,storage,sizeof(saved));
 if(!saved||saved>virtual_partition.size-16)return NULL;
 *size=saved;return storage+16;
}
void lunar_gprof_store_release(void){if(lunar_gprof_inflight())return;heap_caps_free(storage);storage=NULL;}
