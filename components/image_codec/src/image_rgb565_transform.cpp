#include "image_rgb565_transform.h"
#include "sdkconfig.h"
#include <esp_imgfx_crop.h>
#include <esp_imgfx_scale.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>
#include <mutex>

namespace {
// Serial, bounded owner also retains a vendor context if close ever fails.
struct State {
  std::mutex mutex;
  esp_imgfx_crop_handle_t crop=nullptr;
  esp_imgfx_scale_handle_t scale=nullptr;
  std::unique_ptr<uint8_t,decltype(&heap_caps_free)> scratch{nullptr,heap_caps_free};
} state;
bool retire(){
  bool ok=true;
  if(state.scale){if(esp_imgfx_scale_close(state.scale)==ESP_IMGFX_ERR_OK)state.scale=nullptr;else ok=false;}
  if(state.crop){if(esp_imgfx_crop_close(state.crop)==ESP_IMGFX_ERR_OK)state.crop=nullptr;else ok=false;}
  if(ok)state.scratch.reset();
  return ok;
}
}
bool cleanup_rgb565_transforms(){std::lock_guard<std::mutex> lock(state.mutex);return retire();}
bool transform_rgb565_image(const uint16_t *source,size_t source_pixels,uint16_t width,uint16_t height,
    ImageCrop crop,uint16_t *output,size_t output_pixels,uint16_t output_width,uint16_t output_height,
    bool smooth,std::string &error){
  if(!source||!output||!width||!height||width>INT16_MAX||height>INT16_MAX||
      !crop.width||!crop.height||crop.width>width||crop.height>height||crop.x>width-crop.width||crop.y>height-crop.height||
      !output_width||!output_height||output_width>INT16_MAX||output_height>INT16_MAX||
      source_pixels<size_t(width)*height||output_pixels<size_t(output_width)*output_height){error="invalid_rgb565_transform";return false;}
  const auto source_bytes=size_t(width)*height*2,output_bytes=size_t(output_width)*output_height*2;
  const auto from=reinterpret_cast<uintptr_t>(source),to=reinterpret_cast<uintptr_t>(output);
  if((to>=from&&to-from<source_bytes)||(to<from&&from-to<output_bytes)){error="rgb565_transform_overlapping";return false;}
  std::lock_guard<std::mutex> lock(state.mutex);
  if(!retire()){error="image_effects_retiring";return false;}
  esp_imgfx_data_t input{reinterpret_cast<uint8_t *>(const_cast<uint16_t *>(source)),uint32_t(source_bytes)};
  bool ok=true;
  const bool resize=crop.width!=output_width||crop.height!=output_height;
  if(crop.x||crop.y||crop.width!=width||crop.height!=height){
    const auto bytes=size_t(crop.width)*crop.height*2;
    if(resize){
      if(bytes>256*1024){error="image_crop_budget_exceeded";return false;}
#if CONFIG_SPIRAM
      constexpr auto caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
#else
      constexpr auto caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
      if(bytes>64*1024){error="image_crop_budget_exceeded";return false;}
#endif
      state.scratch.reset(static_cast<uint8_t *>(heap_caps_malloc(bytes,caps)));
      if(!state.scratch){error="image_crop_memory_low";return false;}
    }
    esp_imgfx_crop_cfg_t config{{int16_t(width),int16_t(height)},ESP_IMGFX_PIXEL_FMT_RGB565_LE,
      {int16_t(crop.width),int16_t(crop.height)},crop.x,crop.y};
    esp_imgfx_data_t cropped{resize?state.scratch.get():reinterpret_cast<uint8_t *>(output),uint32_t(bytes)};
    ok=esp_imgfx_crop_open(&config,&state.crop)==ESP_IMGFX_ERR_OK&&
      esp_imgfx_crop_process(state.crop,&input,&cropped)==ESP_IMGFX_ERR_OK;
    input=cropped;
  }
  if(ok&&resize){
    esp_imgfx_scale_cfg_t config{{int16_t(crop.width),int16_t(crop.height)},ESP_IMGFX_PIXEL_FMT_RGB565_LE,
      {int16_t(output_width),int16_t(output_height)},smooth?ESP_IMGFX_SCALE_FILTER_TYPE_BILINEAR:ESP_IMGFX_SCALE_FILTER_TYPE_DOWN_RESAMPLE};
    esp_imgfx_data_t result{reinterpret_cast<uint8_t *>(output),uint32_t(output_bytes)};
    ok=esp_imgfx_scale_open(&config,&state.scale)==ESP_IMGFX_ERR_OK&&
      esp_imgfx_scale_process(state.scale,&input,&result)==ESP_IMGFX_ERR_OK;
  }else if(ok&&input.data!=reinterpret_cast<uint8_t *>(output))std::memcpy(output,input.data,output_bytes);
  if(!retire()){error="image_effects_retiring";return false;}
  if(!ok){error="image_effects_transform_failed";return false;}
  error.clear();return true;
}
