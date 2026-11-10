#include "lunaros_micropython_machine.h"
#include "lunaros_micropython_native_modules.h"
#include "link_machine.generated.h"
#include "micropython_engine_hooks.h"
#include <cmath>
#include <cstring>
#ifdef ESP_PLATFORM
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <thread>
#endif
#endif
extern "C" {
#include "py/compile.h"
#include "py/objmodule.h"
#include "py/runtime.h"
}
namespace lunaredu::micropython {
namespace {
uint64_t now_us(){
#ifdef ESP_PLATFORM
  return esp_timer_get_time();
#else
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}
void wait_us(uint64_t duration){
  const uint64_t deadline=now_us()+duration;uint32_t branches=0;
  do{
    if(micropython_engine_vm_checkpoint(&branches)){mp_sched_vm_abort();mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);}
    mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
#ifdef ESP_PLATFORM
    vTaskDelay(1);
#else
#ifdef _WIN32
    Sleep(1);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
#endif
  }while(now_us()<deadline);
}
mp_obj_t delay(mp_obj_t value,double scale){
  const double duration=mp_obj_get_float(value)*scale;
  if(!std::isfinite(duration)||duration<0||duration>60000000)mp_raise_ValueError(MP_ERROR_TEXT("sleep must be 0..60 seconds"));
  wait_us(static_cast<uint64_t>(duration));return mp_const_none;
}
mp_obj_t sleep(mp_obj_t value){return delay(value,1000000);}
mp_obj_t sleep_ms(mp_obj_t value){return delay(value,1000);}
mp_obj_t sleep_us(mp_obj_t value){return delay(value,1);}
mp_obj_t ticks_ms(){return mp_obj_new_int_from_uint((now_us()/1000)&((1u<<30)-1));}
mp_obj_t ticks_us(){return mp_obj_new_int_from_uint(now_us()&((1u<<30)-1));}
mp_obj_t ticks_diff(mp_obj_t a,mp_obj_t b){return mp_obj_new_int(((mp_obj_get_int(a)-mp_obj_get_int(b)+(1<<29))&((1<<30)-1))-(1<<29));}
MP_DEFINE_CONST_FUN_OBJ_1(sleep_obj,sleep);MP_DEFINE_CONST_FUN_OBJ_1(sleep_ms_obj,sleep_ms);MP_DEFINE_CONST_FUN_OBJ_1(sleep_us_obj,sleep_us);
MP_DEFINE_CONST_FUN_OBJ_0(ticks_ms_obj,ticks_ms);MP_DEFINE_CONST_FUN_OBJ_0(ticks_us_obj,ticks_us);MP_DEFINE_CONST_FUN_OBJ_2(ticks_diff_obj,ticks_diff);
void store(mp_obj_t module,const char *name,mp_obj_t value){mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(module)),MP_OBJ_NEW_QSTR(qstr_from_str(name)),value);}
mp_obj_t setup_time(){
  auto module=mp_obj_new_module(qstr_from_str("time"));
  store(module,"sleep",MP_OBJ_FROM_PTR(&sleep_obj));store(module,"sleep_ms",MP_OBJ_FROM_PTR(&sleep_ms_obj));store(module,"sleep_us",MP_OBJ_FROM_PTR(&sleep_us_obj));
  store(module,"ticks_ms",MP_OBJ_FROM_PTR(&ticks_ms_obj));store(module,"ticks_us",MP_OBJ_FROM_PTR(&ticks_us_obj));store(module,"ticks_diff",MP_OBJ_FROM_PTR(&ticks_diff_obj));
  return module;
}
void time_setup(){(void)setup_time();}
mp_obj_t io(size_t count,const mp_obj_t *values){
  const int op=mp_obj_get_int(values[0]);
  if(op==1&&count==2)return sleep_ms(values[1]);
  if(op==125&&count==1)return mp_obj_new_int_from_ull(now_us());
  const auto *port=runtime_machine_port();if(!port)mp_raise_msg(&mp_type_RuntimeError,MP_ERROR_TEXT("machine I/O unavailable on this product"));
  double args[5]={},result=0;size_t arguments=count-1;mp_buffer_info_t buffer{};
  if(op==121){mp_get_buffer_raise(values[count-1],&buffer,MP_BUFFER_READ);--arguments;}
  if(op<100||op>124||arguments>5||buffer.len>32768)mp_raise_ValueError(MP_ERROR_TEXT("invalid machine operation"));
  for(size_t i=0;i<arguments;++i){args[i]=mp_obj_get_float(values[i+1]);if(!std::isfinite(args[i]))mp_raise_ValueError(MP_ERROR_TEXT("invalid machine argument"));}
  const char *error="machine operation failed";
  if(!port->call(port->context,op,args,arguments,static_cast<const uint8_t *>(buffer.buf),buffer.len,result,error))mp_raise_msg_varg(&mp_type_RuntimeError,MP_ERROR_TEXT("%s"),error?error:"machine operation failed");
  if(!std::isfinite(result))mp_raise_ValueError(MP_ERROR_TEXT("invalid machine response"));
  return mp_obj_new_float(result);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(io_obj,1,7,io);
void machine_setup(){
  if(!runtime_machine_port())return;
  auto module=mp_obj_new_module(qstr_from_str("machine"));auto globals=mp_obj_module_get_globals(module);
  store(module,"machine",module);store(module,"time",setup_time());store(module,"_device",MP_OBJ_FROM_PTR(&io_obj));
  const auto old_globals=mp_globals_get(),old_locals=mp_locals_get();
  nlr_buf_t nlr;
  if(nlr_push(&nlr)==0){
    mp_globals_set(globals);mp_locals_set(globals);
    auto lexer=mp_lexer_new_from_str_len(qstr_from_str("<LunarOS machine>"),kLinkMachinePython,std::strlen(kLinkMachinePython),0);
    auto tree=mp_parse(lexer,MP_PARSE_FILE_INPUT);auto code=mp_compile(&tree,qstr_from_str("<LunarOS machine>"),false);mp_call_function_0(code);
    mp_globals_set(old_globals);mp_locals_set(old_locals);nlr_pop();
  }else{mp_globals_set(old_globals);mp_locals_set(old_locals);nlr_jump(nlr.ret_val);}
}
void release(){const auto *port=runtime_machine_port();if(port&&port->release)port->release(port->context);}
}
void setup_micropython_lesson_modules(bool machine){
  (void)register_micropython_native_module({"time",time_setup});
  if(machine)(void)register_micropython_native_module({"machine",machine_setup,release});
}
void release_micropython_machine_outputs(){release();}
}
