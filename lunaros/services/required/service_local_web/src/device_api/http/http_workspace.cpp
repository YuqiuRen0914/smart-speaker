#include "device_api/http/http_workspace.h"
#include <algorithm>
#include <atomic>
#include <new>
#include "memory/json_schema_workspace.h"
#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#include <esp_attr.h>
#include <esp_memory_utils.h>
#include <esp_heap_caps.h>
#endif
namespace {
struct Workspace {HttpRequestContext context;lunaredu::lunaros::JsonWorkspace json{32768};std::unique_ptr<lunaredu::lunaros::JsonSchemaWorkspace> schemas;};
#if defined(ESP_PLATFORM) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
EXT_RAM_BSS_ATTR
#endif
Workspace workspace;
// 0 idle, 1 borrowed, 2 retirement requested/owned. A loan stays valid.
std::atomic<unsigned> state{0};
template<class Container>void clear_bounded(Container &value,size_t capacity){
  value.clear();bool external=true;
#ifdef ESP_PLATFORM
  external=!value.data()||esp_ptr_external_ram(value.data());
#endif
  if(!external||value.capacity()>capacity)Container().swap(value);
}
void clear(HttpRequestContext &c){
  std::fill(c.body.begin(),c.body.end(),0);
  std::fill(c.query.begin(),c.query.end(),0);
  std::fill(c.serialized.begin(),c.serialized.end(),'\0');
  clear_bounded(c.body,8192);clear_bounded(c.query,2048);clear_bounded(c.serialized,8192);
  clear_bounded(c.args,32);clear_bounded(c.pending_headers,32);
  clear_bounded(c.response_timing,128);c.request=nullptr;c.started_us=0;c.method=HTTP_GET;c.json_workspace=nullptr;
}
void release_storage(){
  clear(workspace.context);
  decltype(workspace.context.body)().swap(workspace.context.body);
  decltype(workspace.context.query)().swap(workspace.context.query);
  decltype(workspace.context.serialized)().swap(workspace.context.serialized);
  decltype(workspace.context.args)().swap(workspace.context.args);
  decltype(workspace.context.pending_headers)().swap(workspace.context.pending_headers);
  decltype(workspace.context.response_timing)().swap(workspace.context.response_timing);
  workspace.context.response_headers.release();
  workspace.schemas.reset();workspace.json.retire();
}
}
HttpContextLease::HttpContextLease():context_(&local_),previous_(g_request_context){
  unsigned expected=0;
  if(state.compare_exchange_strong(expected,1)){
    shared_=true;context_=&workspace.context;context_->json_workspace=&workspace.json;
  }
}
HttpContextLease::~HttpContextLease(){
  g_request_context=previous_;clear(*context_);
  if(shared_){
    unsigned expected=1;
    if(!state.compare_exchange_strong(expected,0)){release_storage();state.store(0,std::memory_order_release);}
  }
}
void retire_http_workspace(){
  // httpd_stop has joined its synchronous handler. Never invalidate an active
  // request if this function is reached during failed/partial shutdown.
  unsigned expected=0;
  for(;;){
    if(state.compare_exchange_strong(expected,2)){
      if(expected==0){release_storage();state.store(0,std::memory_order_release);}
      return;
    }
    if(expected==2)return;
    // Retry against a borrowed request; its return performs the release.
  }
}
JsonDocument make_http_json_document(){
  return g_request_context&&g_request_context->json_workspace?
    JsonDocument(g_request_context->json_workspace):JsonDocument();
}

ArduinoJson::Allocator *current_http_json_allocator(){return g_request_context?g_request_context->json_workspace:nullptr;}

std::string *current_http_response_workspace(){return g_request_context?&g_request_context->serialized:nullptr;}

lunaredu::lunaros::JsonSchemaWorkspace *current_http_schema_workspace(){
  if(!g_request_context||g_request_context->json_workspace!=&workspace.json)return nullptr;
#ifdef ESP_PLATFORM
  if(!heap_caps_get_total_size(MALLOC_CAP_SPIRAM))return nullptr;
#endif
  if(!workspace.schemas)workspace.schemas.reset(new(std::nothrow)lunaredu::lunaros::JsonSchemaWorkspace(32768));
  return workspace.schemas.get();
}
