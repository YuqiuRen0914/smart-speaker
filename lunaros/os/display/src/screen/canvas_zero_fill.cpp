#include "diagnostics/function_probe.h"
#include "display/canvas_zero_fill.h"
#include <cstring>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if defined(ESP_PLATFORM) && CONFIG_IDF_TARGET_ESP32S3 && defined(LUNAROS_DISPLAY_DMA2D_AVAILABLE)
#define CANVAS_GDMA_ZERO 1
#include "esp_private/gdma.h"
#include "esp_private/gdma_link.h"
#include "esp_cache.h"
#include "esp_memory_utils.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <atomic>
#include <new>
#include "esp_log.h"
#if defined(LUNAROS_DIAGNOSTIC_PROBES)
#include "soc/gdma_struct.h"
#endif
namespace {
#if defined(LUNAROS_DIAGNOSTIC_PROBES)
void dump_channel(gdma_channel_handle_t handle,bool tx,const char *event){
  if(!handle)return;
  int group=-1,id=-1;
  if(gdma_get_group_channel_id(handle,&group,&id)!=ESP_OK||group!=0||id<0||id>=5)return;
  ESP_LOGI("diag.canvas","event=%s dir=%s group=%d dma=%d conf0=%08lx conf1=%08lx raw=%08lx",event,tx?"tx":"rx",group,id,
    (unsigned long)(tx?GDMA.channel[id].out.conf0.val:GDMA.channel[id].in.conf0.val),
    (unsigned long)(tx?GDMA.channel[id].out.conf1.val:GDMA.channel[id].in.conf1.val),
    (unsigned long)(tx?GDMA.channel[id].out.int_raw.val:GDMA.channel[id].in.int_raw.val));
}
#else
void dump_channel(gdma_channel_handle_t,bool,const char *){}
#endif
constexpr size_t chunk_bytes=2048;
struct ZeroState {
  gdma_channel_handle_t tx=nullptr,rx=nullptr;
  gdma_link_list_handle_t source=nullptr,destination=nullptr;
  void *zeros=nullptr;
  StaticSemaphore_t completion_storage{};
  SemaphoreHandle_t completion=nullptr;
  std::atomic<bool> failed{false};
  size_t maximum=0;
  int core=0;
  TaskHandle_t owner=nullptr;
};
bool completed(gdma_channel_handle_t,gdma_event_data_t *,void *opaque) {
  auto &s=*static_cast<ZeroState *>(opaque);BaseType_t wake=pdFALSE;
  xSemaphoreGiveFromISR(s.completion,&wake);return wake==pdTRUE;
}
bool failed(gdma_channel_handle_t channel,gdma_event_data_t *event,void *opaque) {
  static_cast<ZeroState *>(opaque)->failed.store(true,std::memory_order_release);
  return completed(channel,event,opaque);
}
void stop(ZeroState &s) {
  // A failed stop cannot release an owned canvas still referenced by DMA.
  // Use the existing panic/recovery boundary instead of presenting corruption.
  if((s.tx&&gdma_stop(s.tx)!=ESP_OK)||(s.rx&&gdma_stop(s.rx)!=ESP_OK))
    esp_system_abort("canvas_zero_dma_retirement_failed");
  // STOP requests a descriptor halt; reset also retires both channel FIFOs
  // before fallback, source/descriptor reuse or destruction of borrowed data.
  if((s.tx&&gdma_reset(s.tx)!=ESP_OK)||(s.rx&&gdma_reset(s.rx)!=ESP_OK))
    esp_system_abort("canvas_zero_dma_reset_failed");
}
void destroy(ZeroState *s) {
  if(!s)return;
  stop(*s);
  dump_channel(s->tx,true,"retire");dump_channel(s->rx,false,"retire");
  // IDF 6.1 gdma_del_channel only restores priority. I2S does not set a new
  // owner/writeback strategy when it acquires this hardware slot. Retire our
  // strategy while we still own the stopped channels, including partial init;
  // another live channel can keep the group (and these registers) powered.
  const gdma_strategy_config_t neutral{};
  if((s->tx&&gdma_apply_strategy(s->tx,&neutral)!=ESP_OK)||
     (s->rx&&gdma_apply_strategy(s->rx,&neutral)!=ESP_OK))
    esp_system_abort("canvas_zero_dma_strategy_retirement_failed");
  if(s->tx){(void)gdma_disconnect(s->tx);(void)gdma_del_channel(s->tx);}
  if(s->rx){(void)gdma_disconnect(s->rx);(void)gdma_del_channel(s->rx);}
  if(s->source)(void)gdma_del_link_list(s->source);
  if(s->destination)(void)gdma_del_link_list(s->destination);
  if(s->completion)vSemaphoreDelete(s->completion);
  heap_caps_free(s->zeros);s->~ZeroState();heap_caps_free(s);
}
}
#endif
#ifndef CANVAS_GDMA_ZERO
#define CANVAS_GDMA_ZERO 0
#endif

namespace lunaredu::display {
CanvasZeroFill::~CanvasZeroFill(){release();}
void CanvasZeroFill::release(){
#if CANVAS_GDMA_ZERO
  destroy(static_cast<ZeroState *>(state_));
#endif
  state_=nullptr;attempted_=false;stats_={};
}
bool CanvasZeroFill::initialize(size_t maximum_bytes) {
  if(attempted_)return stats_.ready;
  attempted_=true;
#if CANVAS_GDMA_ZERO
  if(maximum_bytes<4096||maximum_bytes>512*512*2)return false;
  auto *memory=heap_caps_malloc(sizeof(ZeroState),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  if(!memory)return false;
  auto *s=new(memory)ZeroState{};
  s->maximum=maximum_bytes;s->core=xPortGetCoreID();s->owner=xTaskGetCurrentTaskHandle();
  s->completion=xSemaphoreCreateBinaryStatic(&s->completion_storage);
  s->zeros=heap_caps_aligned_calloc(64,1,chunk_bytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
  gdma_channel_alloc_config_t channel{};
  bool ok=s->completion&&s->zeros&&gdma_new_ahb_channel(&channel,&s->tx,&s->rx)==ESP_OK;
  if(ok){
    uint32_t free_ids=0;
    ok=gdma_get_free_m2m_trig_id_mask(s->tx,&free_ids)==ESP_OK&&free_ids;
    if(ok){auto trigger=GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_M2M,0);
      trigger.instance_id=__builtin_ctz(free_ids);
      ok=gdma_connect(s->rx,trigger)==ESP_OK&&gdma_connect(s->tx,trigger)==ESP_OK;}
  }
  if(ok){
    gdma_transfer_config_t transfer{.max_data_burst_size=16,.access_ext_mem=true};
    gdma_strategy_config_t strategy{.owner_check=true,.auto_update_desc=true,.eof_till_data_popped=true};
    ok=gdma_config_transfer(s->tx,&transfer)==ESP_OK&&gdma_config_transfer(s->rx,&transfer)==ESP_OK&&
      gdma_apply_strategy(s->tx,&strategy)==ESP_OK&&gdma_apply_strategy(s->rx,&strategy)==ESP_OK;
  }
  if(ok){
    gdma_link_list_config_t links{};
    links.num_items=(maximum_bytes+chunk_bytes-1)/chunk_bytes;links.item_alignment=4;
    // This exact task retires both channels/FIFOs before any subsequent mount.
    // SDK mount admission otherwise scans the remaining list for EACH buffer,
    // making a quiescent frame's descriptor preparation quadratic. Hardware
    // owner_check above stays enabled; every mounted node is still DMA-owned.
    links.flags.check_owner=false;
    ok=gdma_new_link_list(&links,&s->source)==ESP_OK&&gdma_new_link_list(&links,&s->destination)==ESP_OK;
  }
  if(ok){
    gdma_rx_event_callbacks_t callbacks{};callbacks.on_recv_eof=completed;callbacks.on_descr_err=failed;callbacks.on_descr_empty=failed;
    ok=gdma_register_rx_event_callbacks(s->rx,&callbacks,s)==ESP_OK;
  }
  if(!ok){destroy(s);return false;}
  state_=s;stats_.ready=true;
  dump_channel(s->tx,true,"configured");dump_channel(s->rx,false,"configured");
  // Actual DMA/cache/unaligned-window acceptance before touching a canvas.
  // This allocation is setup-only and is released before initialize returns.
  auto *probe=static_cast<uint8_t *>(heap_caps_aligned_alloc(64,8320,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!probe){release();attempted_=true;return false;}
  std::memset(probe,0xa5,8320);
  const size_t probe_bytes=maximum_bytes<8190?maximum_bytes:8190;
  bool passed=clear(probe+1,probe_bytes)&&stats_.ready;
  for(size_t i=0;i<8320;++i)passed=passed&&(probe[i]==(i>=1&&i<=probe_bytes?0:0xa5));
  heap_caps_free(probe);
  if(!passed){ESP_LOGE("display.zero","GDMA/cache self-test failed");release();attempted_=true;return false;}
  ESP_LOGI("display.zero","GDMA/cache self-test passed maximum_bytes=%u",unsigned(maximum_bytes));
  return true;
#else
  (void)maximum_bytes;return false;
#endif
}
bool CanvasZeroFill::clear(void *pixels,size_t bytes) {
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::Clear);
#if CANVAS_GDMA_ZERO
  auto *s=static_cast<ZeroState *>(state_);
  if(!stats_.ready||!pixels||!s||bytes>s->maximum||bytes<4096||
      xPortGetCoreID()!=s->core||xTaskGetCurrentTaskHandle()!=s->owner||!esp_ptr_external_ram(pixels))return false;
  const size_t line=esp_cache_get_line_size_by_addr(pixels);
  const auto span=canvas_zero_span(reinterpret_cast<uintptr_t>(pixels),bytes,line);
  if(!span.aligned)return false;
  auto *first=static_cast<uint8_t *>(pixels)+span.prefix;
  size_t internal_alignment=0,external_alignment=0;
  if(gdma_get_alignment_constraints(s->rx,&internal_alignment,&external_alignment)!=ESP_OK||
     !external_alignment||external_alignment>line||line%external_alignment)return false;
  const auto recover=[&]{
    stop(*s);++stats_.failures;stats_.ready=false;
    std::memset(pixels,0,bytes);return true;
  };
  size_t remaining=span.aligned,offset=0;int index=0;
  {lunaredu::system::probe::Scope diagnostic_mount(lunaredu::system::probe::Point::ClearMount);
  while(remaining){
    const size_t count=remaining<chunk_bytes?remaining:chunk_bytes;
    gdma_buffer_mount_config_t source{},destination{};
    source.buffer=s->zeros;source.buffer_alignment=64;source.length=count;
    destination.buffer=first+offset;destination.buffer_alignment=line;destination.length=count;
    source.flags.mark_eof=destination.flags.mark_eof=count==remaining;
    source.flags.mark_final=destination.flags.mark_final=count==remaining?GDMA_FINAL_LINK_TO_NULL:GDMA_FINAL_LINK_TO_DEFAULT;
    if(gdma_link_mount_buffers(s->source,index,&source,1,nullptr)!=ESP_OK||
       gdma_link_mount_buffers(s->destination,index,&destination,1,nullptr)!=ESP_OK)return recover();
    offset+=count;remaining-=count;++index;
  }
  }
  while(xSemaphoreTake(s->completion,0)==pdTRUE){}
  s->failed.store(false,std::memory_order_release);
  // The complete owned frame is being discarded, so dirty old pixel cache
  // lines need no writeback. Both partial boundary lines remain CPU-owned.
  {lunaredu::system::probe::Scope diagnostic_cache(lunaredu::system::probe::Point::ClearCacheBefore);
  if(esp_cache_msync(first,span.aligned,ESP_CACHE_MSYNC_FLAG_DIR_M2C)!=ESP_OK)return recover();}
  bool ok=gdma_reset(s->tx)==ESP_OK&&gdma_reset(s->rx)==ESP_OK&&
      gdma_start(s->rx,gdma_link_get_head_addr(s->destination))==ESP_OK&&
      gdma_start(s->tx,gdma_link_get_head_addr(s->source))==ESP_OK;
  {lunaredu::system::probe::Scope diagnostic_wait(lunaredu::system::probe::Point::ClearWait);
  if(ok)ok=xSemaphoreTake(s->completion,pdMS_TO_TICKS(100))==pdTRUE&&!s->failed.load(std::memory_order_acquire);
  }
  stop(*s);
  // Retire and invalidate before permitting any CPU drawing/presentation.
  {lunaredu::system::probe::Scope diagnostic_cache(lunaredu::system::probe::Point::ClearCacheAfter);
  if(esp_cache_msync(first,span.aligned,ESP_CACHE_MSYNC_FLAG_DIR_M2C)!=ESP_OK)ok=false;}
  if(!ok)return recover();
  std::memset(pixels,0,span.prefix);
  std::memset(first+span.aligned,0,span.suffix);
  ++stats_.calls;stats_.bytes+=bytes;return true;
#else
  (void)pixels;(void)bytes;return false;
#endif
}
}
