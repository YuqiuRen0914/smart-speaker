#include "lunaros_micropython_app_backend.h"
#include "lunaros_micropython_runtime_config.h"
#include "lunaros_micropython_runtime.h"
#include "app/execution/application_backend.h"
#include "kernel/runtime.h"
#include "micropython_application_owner.h"
#include "python_application_vm.h"
#include <cstdlib>
namespace lunaredu::micropython {
namespace {
using namespace lunaros::apps;
class PythonSession final : public ApplicationSession {
public:
  explicit PythonSession(lunaros::KernelRuntime &runtime):runtime_(runtime){}
  ~PythonSession() override {
    std::string ignored;
    if(!close(ignored)&&(owned_||devices_.retained()))std::abort();
  }
  bool open(const uint8_t *p,size_t n,SessionStorage s,uint32_t seed,std::string &error) override {
    if((owned_||devices_.retained())&&!close(error))return false;
    if(!validate_application_entry("micropython",p,n)){error="invalid Python application source";return false;}
    if(!ensure_runtime_started(error))return false;
    bytes_=p;size_=n;storage_=s;seed_=seed;
    return invoke(Operation::Open,error);
  }
  bool event(int k,int a,int b,int c,std::string &e) override {values_[0]=k;values_[1]=a;values_[2]=b;values_[3]=c;return invoke(Operation::Event,e);}
  bool checkpoint(std::string &e) override {return !owned_||invoke(Operation::Save,e);}
  bool validate_save(SessionStorage s,std::string &e) override {candidate_=s;return owned_&&invoke(Operation::Validate,e);}
  bool close(std::string &error) override {
    if((owned_||devices_.retained())&&!invoke(Operation::Close,error))return false;
    return micropython_shutdown_idle(runtime_,error);
  }
  bool active() const override {return vm_.active();}
  std::shared_ptr<const DrawList> frame() const override {return vm_.frame();}
private:
  enum class Operation {Open,Event,Save,Validate,Close};
  bool invoke(Operation op,std::string &error){
    operation_=op;
    return runtime_.invoke_executor(lunaredu::micropython::kRuntimeExecutorId,[](void *p,lunaros::ServiceOperationResult &r){
      auto &s=*static_cast<PythonSession *>(p);
      if(s.operation_==Operation::Open){
        if(!s.devices_.bind(s.storage_.device,s.storage_.validation_only,r.error)){r.ok=false;return;}
        if(!acquire_micropython_application_on_owner(r.error)){r.ok=false;return;}
        s.owned_=true;r.ok=s.vm_.open(s.bytes_,s.size_,s.storage_,s.seed_,r.error);
      }else if(s.operation_==Operation::Event)r.ok=s.vm_.event(s.values_[0],s.values_[1],s.values_[2],s.values_[3],r.error);
      else if(s.operation_==Operation::Save)r.ok=s.vm_.checkpoint(r.error);
      else if(s.operation_==Operation::Validate)r.ok=s.vm_.validate_save(s.candidate_,r.error);
      else {
        s.devices_.revoke();
        if(!s.devices_.retire(r.error)){r.ok=false;return;}
        s.vm_.close();
      }
      if(s.owned_&&!s.vm_.active()){
        s.devices_.revoke();
        if(!s.devices_.retire(r.error)){r.ok=false;return;}
        s.vm_.close();release_micropython_application_on_owner();s.owned_=false;
      }
    },this,error);
  }
  lunaros::KernelRuntime &runtime_;
  PythonApplicationVm vm_;
  SessionDeviceScope devices_;
  bool owned_=false;
  Operation operation_=Operation::Close;
  const uint8_t *bytes_=nullptr;size_t size_=0;uint32_t seed_=0;
  int values_[4]{};SessionStorage storage_{},candidate_{};
};
std::unique_ptr<ApplicationSession> create(lunaros::KernelRuntime &runtime){return std::make_unique<PythonSession>(runtime);}
}
bool register_application_backend(){return !core_enabled() || lunaros::apps::register_application_backend("micropython",create);}
}
