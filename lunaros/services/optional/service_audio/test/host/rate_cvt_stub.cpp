#include <cstdint>
#include <cassert>
#include <new>
#include <esp_ae_rate_cvt.h>
#include "rate_cvt_test_control.h"
namespace {
// API/ownership double only: deliberately no FIR or sound-quality simulation.
struct State { uint32_t input,output,phase=0; };
}
extern "C" esp_ae_err_t esp_ae_rate_cvt_open(esp_ae_rate_cvt_cfg_t *cfg,void **handle) {
  *handle=nullptr;
  if(rate_cvt_test::fail_open)return ESP_AE_ERR_MEM_LACK;
  assert(cfg->channel==1&&cfg->bits_per_sample==16&&cfg->complexity==2);
  *handle=new(std::nothrow) State{cfg->src_rate,cfg->dest_rate};
  if(!*handle)return ESP_AE_ERR_MEM_LACK;
  ++rate_cvt_test::opens;return ESP_AE_ERR_OK;
}
extern "C" void esp_ae_rate_cvt_close(void *handle) {
  if(handle){delete static_cast<State *>(handle);++rate_cvt_test::closes;}
}
extern "C" esp_ae_err_t esp_ae_rate_cvt_reset(void *handle) {
  if(rate_cvt_test::fail_reset)return ESP_AE_ERR_FAIL;
  static_cast<State *>(handle)->phase=0;return ESP_AE_ERR_OK;
}
extern "C" esp_ae_err_t esp_ae_rate_cvt_get_max_out_sample_num(void *handle,uint32_t count,uint32_t *out) {
  const auto &s=*static_cast<State *>(handle);
  *out=uint32_t((uint64_t(count)*s.output+s.input-1)/s.input)+128;
  return ESP_AE_ERR_OK;
}
extern "C" esp_ae_err_t esp_ae_rate_cvt_process(void *handle,void *input,uint32_t count,void *output,uint32_t *out) {
  ++rate_cvt_test::calls;
  if(rate_cvt_test::fail_process)return ESP_AE_ERR_FAIL;
  assert(count==1); // Vendor boundary has one fixed quantum across all packets.
  auto &s=*static_cast<State *>(handle);
  uint32_t maximum=0;esp_ae_rate_cvt_get_max_out_sample_num(handle,count,&maximum);
  assert(*out>=maximum);
  s.phase+=s.output;*out=0;
  while(s.phase>=s.input){s.phase-=s.input;static_cast<int16_t *>(output)[(*out)++]=*static_cast<int16_t *>(input);}
  return ESP_AE_ERR_OK;
}
