#include "python_application_vm.h"
#include "app/execution/draw_list.h"
#include "app/package/application_profile.h"
#include "native/nlr_cleanup.h"
#include "micropython_engine_hooks.h"
extern "C" {
#include "py/compile.h"
#include "py/lexer.h"
#include "py/objmodule.h"
#include "py/runtime.h"
#include "py/nlr.h"
}
using namespace lunaredu::lunaros::apps;
namespace lunaredu::micropython {
namespace {
PythonApplicationVm *owner=nullptr;
PythonApplicationVm &require_owner() {
  if(!owner)mp_raise_msg(&mp_type_RuntimeError,MP_ERROR_TEXT("no active application"));
  return *owner;
}
mp_obj_t graphics_submit(mp_obj_t object) {
  auto &s=require_owner();mp_buffer_info_t buffer;mp_get_buffer_raise(object,&buffer,MP_BUFFER_READ);
  if(!s.credit(s.submissions_,2)||buffer.len>32768)
    mp_raise_msg(&mp_type_RuntimeError,MP_ERROR_TEXT("graphics budget exceeded"));
  auto draw=std::make_shared<DrawList>();NlrCleanup cleanup(draw);
  if(!decode_draw_list(static_cast<const uint8_t *>(buffer.buf),buffer.len,*draw,{kApplicationBudget.draw_bytes,kApplicationBudget.draw_commands,kApplicationPlatform.canvas_width,kApplicationPlatform.canvas_height}))
    mp_raise_ValueError(MP_ERROR_TEXT("invalid graphics commands"));
  s.pending_=std::move(draw);return mp_obj_new_int(0);
}
MP_DEFINE_CONST_FUN_OBJ_1(graphics_submit_obj,graphics_submit);
mp_obj_t storage_read() {
  auto &s=require_owner();
  if(!s.storage_.read)mp_raise_msg(&mp_type_OSError,MP_ERROR_TEXT("private storage unavailable"));
  if(!s.credit(s.reads_,4))mp_raise_msg(&mp_type_RuntimeError,MP_ERROR_TEXT("storage read budget exceeded"));
  std::vector<uint8_t> bytes(8192);NlrCleanup cleanup(bytes);
  const int n=s.storage_.read(s.storage_.context,bytes.data(),bytes.size());
  if(n<0||n>8192)mp_raise_msg(&mp_type_OSError,MP_ERROR_TEXT("storage read failed"));
  return mp_obj_new_bytes(bytes.data(),size_t(n));
}
MP_DEFINE_CONST_FUN_OBJ_0(storage_read_obj,storage_read);
mp_obj_t storage_write(mp_obj_t object) {
  auto &s=require_owner();mp_buffer_info_t buffer;mp_get_buffer_raise(object,&buffer,MP_BUFFER_READ);
  if(!s.storage_.write)mp_raise_msg(&mp_type_OSError,MP_ERROR_TEXT("private storage unavailable"));
  if(!s.credit(s.writes_,1)||buffer.len==0||buffer.len>8192)
    mp_raise_msg(&mp_type_RuntimeError,MP_ERROR_TEXT("storage write budget exceeded"));
  if(!s.storage_.write(s.storage_.context,static_cast<const uint8_t *>(buffer.buf),buffer.len))
    mp_raise_msg(&mp_type_OSError,MP_ERROR_TEXT("storage write failed"));
  return mp_obj_new_int_from_uint(buffer.len);
}
MP_DEFINE_CONST_FUN_OBJ_1(storage_write_obj,storage_write);
mp_obj_t log_write(mp_obj_t object) {
  auto &s=require_owner();size_t n=0;const char *p=mp_obj_str_get_data(object,&n);
  if(!s.credit(s.logs_,4)||n>256)mp_raise_msg(&mp_type_RuntimeError,MP_ERROR_TEXT("log budget exceeded"));
  micropython_engine_stdout_write(p,n);return mp_obj_new_int_from_uint(n);
}
MP_DEFINE_CONST_FUN_OBJ_1(log_write_obj,log_write);
void setup_module() {
  const auto module=mp_obj_new_module(qstr_from_str("lunaros_app_v1"));
  const auto globals=MP_OBJ_FROM_PTR(mp_obj_module_get_globals(module));
  mp_obj_dict_store(globals,MP_OBJ_NEW_QSTR(qstr_from_str("graphics_submit")),MP_OBJ_FROM_PTR(&graphics_submit_obj));
  mp_obj_dict_store(globals,MP_OBJ_NEW_QSTR(qstr_from_str("storage_read")),MP_OBJ_FROM_PTR(&storage_read_obj));
  mp_obj_dict_store(globals,MP_OBJ_NEW_QSTR(qstr_from_str("storage_write")),MP_OBJ_FROM_PTR(&storage_write_obj));
  mp_obj_dict_store(globals,MP_OBJ_NEW_QSTR(qstr_from_str("log_write")),MP_OBJ_FROM_PTR(&log_write_obj));
}
}
bool python_application_budget_abort(){return owner&&owner->budget_abort();}
bool PythonApplicationVm::budget_abort() {
  if(!calling_)return false;
  if(++branches_>20000||std::chrono::steady_clock::now()>deadline_)expired_=true;
  return expired_;
}
bool PythonApplicationVm::credit(unsigned &counter,unsigned maximum){return ++counter<=maximum;}
void PythonApplicationVm::begin(){
  pending_.reset();submissions_=reads_=writes_=logs_=branches_=0;
  calling_=true;expired_=false;deadline_=std::chrono::steady_clock::now()+std::chrono::milliseconds(200);
}
bool PythonApplicationVm::finish_error(void *exception,std::string &error){
  calling_=false;pending_.reset();
  if(expired_)error="Python callback budget exceeded";
  else if(!exception)error="Python application stopped";
  else {error=std::string("python_exception:")+mp_obj_get_type_str(MP_OBJ_FROM_PTR(exception));mp_obj_print_exception(&mp_plat_print,MP_OBJ_FROM_PTR(exception));}
  return false;
}
bool PythonApplicationVm::open(const uint8_t *source,size_t size,SessionStorage storage,uint32_t seed,std::string &error){
  if(owner||active_){error="another Python application is running";return false;}
  owner=this;storage_=storage;begin();
  nlr_buf_t nlr;nlr_set_abort(&nlr);
  if(nlr_push(&nlr)==0){
    setup_module();
    auto lexer=mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_,reinterpret_cast<const char *>(source),size,0);
    const qstr filename=lexer->source_name;
    auto tree=mp_parse(lexer,MP_PARSE_FILE_INPUT);
    auto function=mp_compile(&tree,filename,true);mp_call_function_0(function);
    if(std::chrono::steady_clock::now()>deadline_){expired_=true;mp_raise_msg(&mp_type_RuntimeError,MP_ERROR_TEXT("initialization deadline exceeded"));}
    // Required functions live in VM globals, keeping them GC-rooted.
    for(const auto name:{"luna_init","luna_event","luna_stop"})
      if(!mp_obj_is_callable(mp_load_global(qstr_from_str(name))))mp_raise_TypeError(MP_ERROR_TEXT("application entry must be callable"));
    nlr_pop();nlr_set_abort(nullptr);calling_=false;
  }else{nlr_set_abort(nullptr);finish_error(nlr.ret_val,error);owner=nullptr;storage_={};return false;}
  active_=true;const intptr_t args[]{240,240,intptr_t(seed)};
  if(!call("luna_init",3,args,false,error)||!latest_){if(error.empty())error="Python application produced no initial frame";close();return false;}
  return true;
}
bool PythonApplicationVm::call(const char *name,size_t argc,const intptr_t *values,bool optional,std::string &error){
  error.clear();begin();nlr_buf_t nlr;nlr_set_abort(&nlr);
  if(nlr_push(&nlr)==0){
    const auto key=MP_OBJ_NEW_QSTR(qstr_from_str(name));
    const auto entry=mp_map_lookup(&mp_globals_get()->map,key,MP_MAP_LOOKUP);
    if(!entry&&optional){nlr_pop();nlr_set_abort(nullptr);calling_=false;return true;}
    if(!entry)mp_raise_msg(&mp_type_RuntimeError,MP_ERROR_TEXT("missing application callback"));
    mp_obj_t args[4];for(size_t i=0;i<argc;++i)args[i]=mp_obj_new_int(values[i]);
    const auto result=mp_call_function_n_kw(entry->value,argc,0,args);
    if(mp_obj_get_int(result)!=0)mp_raise_msg(&mp_type_RuntimeError,MP_ERROR_TEXT("application callback rejected operation"));
    if(std::chrono::steady_clock::now()>deadline_){expired_=true;mp_raise_msg(&mp_type_RuntimeError,MP_ERROR_TEXT("callback deadline exceeded"));}
    nlr_pop();nlr_set_abort(nullptr);calling_=false;
    if(pending_)latest_=std::move(pending_);
    return true;
  }
  nlr_set_abort(nullptr);return finish_error(nlr.ret_val,error);
}
bool PythonApplicationVm::event(int k,int a,int b,int c,std::string &e){
  const intptr_t args[]{k,a,b,c};if(!active_||!call("luna_event",4,args,false,e)){close();return false;}return true;
}
bool PythonApplicationVm::checkpoint(std::string &e){
  if(!active_)return true;
  if(!call("luna_save",0,nullptr,true,e)){close();return false;}
  return true;
}
bool PythonApplicationVm::validate_save(SessionStorage candidate,std::string &e){
  if(!active_){e="Python application is inactive";return false;}
  const auto original=storage_;storage_=lunaros::apps::session_validation_storage(original,candidate);
  const bool ok=call("luna_validate_save",0,nullptr,false,e);storage_=original;return ok;
}
void PythonApplicationVm::close(){
  if(active_){std::string ignored;(void)call("luna_stop",0,nullptr,false,ignored);}
  active_=false;calling_=false;storage_={};latest_.reset();pending_.reset();
  if(owner==this)owner=nullptr;
}
}
