#include "lunaros_applications_service_id.h"
#include "applications_service_internal.h"
#include "app/application_installation.h"
#include "app/package/save_format.h"
#include "app/package/package_validation.h"
#include "app/package/package_trust.h"
#include "app/package/application_limits.h"
#include "app/package/application_data_ports.h"
#include "lunaros_storage.h"
#include "app/application_persistence.h"
#include <algorithm>
#include <cstring>
#include <ctime>
namespace lunaredu::lunaros::apps {
using namespace runtime_detail;
namespace {
// Internal JSON sidecar contains only timestamp/hash, not application data.
constexpr size_t kMaximumSaveMetadataBytes = 1024;
bool reserve_save(const std::string &id,const void *allowed_owner,
    ApplicationIdentityReservation &reservation,Application &selected,std::string &error) {
  error.clear();
  if(!id_valid(id)){error="application_identity_invalid";return false;}
  std::lock_guard lock(catalog_mutex);
  if(owned_by_other(allowed_owner,id)){error="application_still_running";return false;}
  std::vector<Application> current;
  if(!read_catalog(current)){error="installed_catalog_invalid";return false;}
  const auto entry=std::find_if(current.begin(),current.end(),[&](const auto &a){return a.id==id&&a.private_storage;});
  if(entry==current.end()){error="应用未安装或没有私有存储";return false;}
  selected=*entry;reservation.reserve_locked(id);return true;
}
bool read_saved_bytes(const std::string &id,SaveInfo &info,std::vector<uint8_t> &bytes,std::string &error){
  info={};bytes.clear();const auto path="/managed/apps/"+id+"/save.bin";
  if(!storage_path_exists(path.c_str()))return true;
  if(!storage_read_file_bounded(path.c_str(),kMaximumSaveBytes,bytes)||bytes.empty()){error="存档读取失败";return false;}
  info.bytes=uint32_t(bytes.size());std::vector<uint8_t> metadata;ApplicationSaveMetadata record;
  if(storage_read_file_bounded((path+".meta").c_str(),kMaximumSaveMetadataBytes,metadata)&&
      decode_save_metadata(metadata,record)&&record.hash==save_hash(bytes.data(),bytes.size()))info.timestamp=record.timestamp;
  return true;
}
struct SaveValidationContext {
  KernelRuntime *runtime=nullptr;
  AppExecutor session;
  std::shared_ptr<std::vector<uint8_t>> candidate;
  RetiredApplicationOwner retirement;
};
}
SessionStorage ApplicationsService::Impl::storage_callbacks() {
  return {
      this,
      [](void *p, uint8_t *out, size_t capacity) {
        auto &state = *static_cast<Impl *>(p);
        std::vector<uint8_t> saved;
        if (!storage_read_file_bounded(state.save_path.c_str(), std::min(capacity,kMaximumSaveBytes), saved))
          return -1;
        memcpy(out, saved.data(), saved.size());
        return int(saved.size());
      },
      [](void *p, const uint8_t *data, size_t size) {
        auto &state = *static_cast<Impl *>(p);
        if(size>kMaximumSaveBytes || size==0)return false;
        if(state.last_saved.size()==size && std::equal(data,data+size,state.last_saved.begin()))return true;
        if(!storage_write_file(state.save_path.c_str(), data, size))return false;
        state.last_saved.assign(data,data+size);
        const auto now=std::time(nullptr);
        const auto text=encode_save_metadata({now>=1577836800?uint32_t(now):0,save_hash(data,size)});
        (void)storage_write_text_file((state.save_path+".meta").c_str(),text.c_str());
        return true;
      }};
}
bool ApplicationsService::save_info(const std::string &id,SaveInfo &info,std::string &error) {
  Impl::Admission admission(*impl_, error);
  if (!admission) return false;
  std::vector<uint8_t> bytes;
  return impl_->read_save(id,info,bytes,error);
}
bool ApplicationsService::Impl::save_access(const std::string &id,ApplicationIdentityReservation &reservation,std::string &error,Application *selected) {
  error.clear();
  if (occupied_id == id && (active_id.empty() || retiring)) {
    error="application_still_running"; return false;
  }
  Application value;
  if(!reserve_save(id,this,reservation,value,error))return false;
  if(selected)*selected=std::move(value);
  return true;
}
bool ApplicationsService::Impl::read_save(const std::string &id,SaveInfo &info,
    std::vector<uint8_t> &bytes,std::string &error) {
  info={};bytes.clear();
  ApplicationIdentityReservation reservation;
  if(!save_access(id,reservation,error))return false;
  if(active_id==id && session.active() && !session.checkpoint(error))return false;
  return read_saved_bytes(id,info,bytes,error);
}
bool ApplicationsService::export_save(const std::string &id,std::vector<uint8_t> &out,std::string &error) {
  Impl::Admission admission(*impl_, error);
  if (!admission) return false;
  SaveInfo info;std::vector<uint8_t> data;
  if(!impl_->read_save(id,info,data,error))return false;
  if(!info.bytes){error="还没有存档";return false;}
  if(!encode_save_file(id,data,info.timestamp,out,kMaximumSaveBytes)){error="导出失败";return false;}
  return true;
}
bool ApplicationsService::request_save(SaveRequest request,const std::string &id,
    ApplicationSaveTicket &ticket,std::string &error,std::vector<uint8_t> input) {
  ticket={};error.clear();
  if(!ready()){error="applications_service_unavailable";return false;}
  if(request!=SaveRequest::Checkpoint&&!id_valid(id)){error="application_identity_invalid";return false;}
  if(request==SaveRequest::Import){
    if(input.size()<28||input.size()>kMaximumSaveFileBytes){error="invalid_save_size";return false;}
    if(running()){error="请先退出应用再导入存档";return false;}
  }else if(!input.empty()){error="unexpected_save_payload";return false;}
  if(request!=SaveRequest::Checkpoint&&!impl_->maintenance.wake_bound()){
    error="application_io_unavailable";return false;
  }
  auto work=std::make_shared<Impl::ApplicationWork>();
  using Kind=Impl::ApplicationWork::Kind;
  switch(request){
    case SaveRequest::Checkpoint:work->kind=Kind::Checkpoint;break;
    case SaveRequest::Info:work->kind=Kind::SaveInfo;break;
    case SaveRequest::Export:work->kind=Kind::ExportSave;break;
    case SaveRequest::Reset:work->kind=Kind::ResetSave;break;
    case SaveRequest::Import:work->kind=Kind::ImportSave;break;
    default:error="application_save_request_invalid";return false;
  }
  work->id=request==SaveRequest::Checkpoint?impl_->active_id:id;
  work->save=std::make_shared<ApplicationSaveData>();work->input=std::move(input);work->ready_for_io=false;
  work->allow_foreground_read=work->id==impl_->active_id&&!impl_->retiring&&running();
  const bool observation=request==SaveRequest::Info||request==SaveRequest::Export;
  if(observation&&impl_->occupied_id==id&&!work->allow_foreground_read){error="application_still_running";return false;}
  work->wait_for_retirement=(request==SaveRequest::Import&&!impl_->occupied_id.empty())||
      (request==SaveRequest::Reset&&impl_->occupied_id==id);
  if(!impl_->queue_work(work,error))return false;
  if((request==SaveRequest::Checkpoint&&running())||(observation&&work->allow_foreground_read)){
    if(!impl_->session.begin_checkpoint(work->checkpoint,error)){
      impl_->finish_work(work,false,error);return false;
    }
  }
  ticket.completion_=work->result.receipt();ticket.data_=work->save;
  (void)poll_application_work();return true;
}
bool ApplicationsService::poll_application_work(){
  std::shared_ptr<Impl::ApplicationWork> work;
  {
    std::lock_guard lock(impl_->admission_mutex);
    const auto &current=impl_->application_work;
    if(impl_->phase.load()!=Impl::Phase::Ready||!current||current->started||current->ready_for_io||!current->result.pending())return false;
    work=current;
  }
  if(work->checkpoint){
    const auto result=work->checkpoint.result();
    if(result.phase==ApplicationOperationPhase::Pending)return false;
    if(result.phase!=ApplicationOperationPhase::Succeeded){impl_->finish_work(work,false,result.error);return true;}
  }
  if(work->wait_for_retirement&&!impl_->occupied_id.empty()){
    std::string error;
    if(!close(error,0)){
      if(error=="application_stop_timeout"||error=="application_catalog_busy"||error=="application_device_retirement_pending")return false;
      impl_->finish_work(work,false,std::move(error));return true;
    }
  }
  if(work->kind==Impl::ApplicationWork::Kind::Checkpoint){impl_->finish_work(work,true,{});return true;}
  {
    std::lock_guard lock(impl_->admission_mutex);
    if(impl_->application_work!=work||impl_->phase.load()!=Impl::Phase::Ready)return false;
    work->ready_for_io=true;
  }
  impl_->maintenance.schedule(0);impl_->maintenance.poll();return true;
}
bool ApplicationsService::Impl::execute_save_work(const std::shared_ptr<ApplicationWork> &work,std::string &error){
  error.clear();using Kind=ApplicationWork::Kind;
  ApplicationIdentityReservation reservation;Application selected;
  if(!reserve_save(work->id,work->allow_foreground_read?this:nullptr,reservation,selected,error))return false;
  if(work->kind==Kind::SaveInfo||work->kind==Kind::ExportSave){
    std::vector<uint8_t> bytes;
    if(!read_saved_bytes(work->id,work->save->info,bytes,error))return false;
    if(work->kind==Kind::SaveInfo)return true;
    if(bytes.empty()){error="还没有存档";return false;}
    if(!encode_save_file(work->id,bytes,work->save->info.timestamp,work->save->bytes,kMaximumSaveBytes)){
      error="导出失败";return false;
    }
    return true;
  }
  const auto path="/managed/apps/"+work->id+"/save.bin";
  if(work->kind==Kind::ResetSave){
    if(storage_path_exists(path.c_str())&&!storage_delete_file(path.c_str())){error="重置失败";return false;}
    if(storage_path_exists((path+".meta").c_str())&&!storage_delete_file((path+".meta").c_str())){
      error="存档已重置，元数据清理失败";return false;
    }
    return true;
  }
  if(work->kind!=Kind::ImportSave){error="application_save_request_invalid";return false;}
  auto context=std::make_unique<SaveValidationContext>();context->runtime=runtime;
  context->candidate=std::make_shared<std::vector<uint8_t>>();
  const bool decoded=decode_save_file(work->id,work->input.data(),work->input.size(),*context->candidate,kMaximumSaveBytes);
  std::vector<uint8_t>().swap(work->input); // Retire raw transfer before package/VM allocation.
  if(!decoded){error="备份损坏或属于其他应用";return false;}
  if(!application_backend_available(selected.entry_format)){error="应用执行后端不可用";return false;}
  Application checked;ApplicationEntry entry;
  if(!read_verified_package(selected,entry.bytes,checked,entry.offset,error))return false;
  entry.backend=checked.entry_format;entry.seed=1;entry.tick_hz=0;
  entry.storage={context->candidate.get(),[](void *p,uint8_t *out,size_t capacity){
    const auto &bytes=*static_cast<const std::vector<uint8_t> *>(p);
    if(bytes.size()>capacity)return -1;
    std::memcpy(out,bytes.data(),bytes.size());return int(bytes.size());
  },nullptr};
  {
    std::lock_guard lock(catalog_mutex);
    active_owners[context.get()]=work->id;
  }
  // The temporary VM has its own executor/callback owner. It never borrows the
  // Main foreground session, and candidate validation cannot write save files.
  entry.storage.validation_only=true;
  const auto storage=entry.storage;
  const bool valid=context->session.start(*runtime,error,kApplicationsServiceId)&&
      context->session.open(std::move(entry),error)&&context->session.validate_save(storage,error);
  std::string cleanup;
  if(!context->session.close(cleanup)){
    auto &retirement=context->retirement;
    retirement.context=context.get();retirement.runtime=runtime;
    retirement.stop=[](void *p){std::string detail;return static_cast<SaveValidationContext *>(p)->session.close(detail,0);};
    retirement.destroy=[](void *p){delete static_cast<SaveValidationContext *>(p);};
    adopt_retired_application(retirement);(void)context.release();
    error=std::move(cleanup);return false; // No later candidate write after an ambiguous/failed close.
  }
  bool ok=valid;
  if(ok&&phase.load()!=Phase::Ready){ok=false;error="applications_service_stopping";}
  if(ok&&!storage_write_file(path.c_str(),context->candidate->data(),context->candidate->size())){
    ok=false;error="导入写入失败";
  }
  if(ok)(void)storage_delete_file((path+".meta").c_str());
  {
    std::lock_guard lock(catalog_mutex);active_owners.erase(context.get());
  }
  if(ok)error.clear();
  return ok;
}
bool ApplicationsService::reset_save(const std::string &id,std::string &error) {
  Impl::Admission admission(*impl_, error);
  if (!admission) return false;
  if(impl_->occupied_id==id && !close(error))return false;
  ApplicationIdentityReservation reservation;
  if(!impl_->save_access(id,reservation,error))return false;
  const auto path="/managed/apps/"+id+"/save.bin";
  if(storage_path_exists(path.c_str()) && !storage_delete_file(path.c_str())){error="重置失败";return false;}
  if(storage_path_exists((path+".meta").c_str()) && !storage_delete_file((path+".meta").c_str())) {
    error="存档已重置，元数据清理失败"; return false;
  }
  if (impl_->save_path == path) impl_->last_saved.clear();
  return true;
}
bool ApplicationsService::import_save(const std::string &id,const uint8_t *data,size_t size,std::string &error) {
  Impl::Admission admission(*impl_, error);
  if (!admission) return false;
  if(running()){error="请先退出应用再导入存档";return false;}
  auto candidate = std::make_shared<std::vector<uint8_t>>();
  if(!decode_save_file(id,data,size,*candidate,kMaximumSaveBytes)){error="备份损坏或属于其他应用";return false;}
  std::vector<uint8_t> package;Application app;size_t offset=0;
  // A failed stop can outlive this call. Its callbacks must never borrow the
  // local vector, and another import must not overwrite a retiring context.
  if (!close(error)) return false;
  Application selected;
  ApplicationIdentityReservation reservation;
  if(!impl_->save_access(id,reservation,error,&selected))return false;
  if(!application_backend_available(selected.entry_format)){error="应用执行后端不可用";return false;}
  {
    std::lock_guard lock(catalog_mutex);
    impl_->occupied_id=id;
    active_owners[impl_.get()]=id;
  }
  if(!read_verified_package(selected,package,app,offset,error)){
    std::string cleanup;(void)close(cleanup);return false;
  }
  impl_->validation_save = candidate;
  SessionStorage storage{candidate.get(),[](void *ctx,uint8_t *p,size_t n){const auto &v=*static_cast<std::vector<uint8_t> *>(ctx);if(v.size()>n)return -1;memcpy(p,v.data(),v.size());return int(v.size());},nullptr};
  if (!impl_->runtime || !impl_->session.start(*impl_->runtime, error, kApplicationsServiceId)) {
    if (error.empty()) error = "应用运行服务未启动";
    std::string cleanup; if (!close(cleanup)) error=cleanup;
    return false;
  }
  ApplicationEntry entry{std::move(package),offset,storage,1,0,app.entry_format};
  entry.storage.validation_only=true;
  bool ok=impl_->session.open(std::move(entry),error);
  if(ok)ok=impl_->session.validate_save(storage,error);
  std::string cleanup;
  if (!impl_->session.close(cleanup)) { impl_->retiring=true; error = impl_->error=cleanup; return false; }
  // Keep the reservation through commit so another channel cannot reset,
  // reinstall or uninstall between successful validation and write.
  const auto path="/managed/apps/"+id+"/save.bin";
  if(ok && !storage_write_file(path.c_str(),candidate->data(),candidate->size())){error="导入写入失败";ok=false;}
  if(ok)(void)storage_delete_file((path+".meta").c_str());
  {
    std::lock_guard lock(catalog_mutex);
    active_owners.erase(impl_.get());impl_->occupied_id.clear();impl_->retiring=false;impl_->validation_save.reset();
  }
  if (ok) error.clear();
  return ok;
}
} // namespace lunaredu::lunaros::apps

namespace lunaredu::lunaros::apps {
ApplicationDataPorts application_data_ports(ApplicationsService &runtime) {
  ApplicationDataPorts ports; ports.context = &runtime;
  ports.uninstall = [](void *p,const std::string &id,bool delete_data,ApplicationOperation &receipt,std::string &error) {
    return static_cast<ApplicationsService *>(p)->request_uninstall(id,delete_data,receipt,error);
  };
  ports.catalog = [](void *p, ApplicationList &out, std::string &error) {
    auto &runtime = *static_cast<ApplicationsService *>(p);
    // Settings observes the catalog published by its lifecycle owner. Opening
    // or repainting a view must not reread the filesystem on the input task.
    if (!runtime.ready()) { error = "applications_service_unavailable"; return false; }
    runtime.observe_catalog();
    out = runtime.catalog_snapshot().application_list(); error.clear(); return true;
  };
  ports.observe_revision = [](void *p,uint64_t &revision) {
    auto &runtime=*static_cast<ApplicationsService *>(p);
    if(!runtime.ready())return false;
    runtime.observe_catalog();revision=runtime.installed_revision();return true;
  };
  return ports;
}
}

namespace lunaredu::lunaros::apps {
bool ApplicationsService::backup_exists(const std::string &id) {
  std::string error;
  Impl::Admission admission(*impl_, error);
  return admission && id_valid(id) && storage_path_exists(("/saves/"+id+".lsave").c_str());
}
bool ApplicationsService::execute_save(const std::string &id, SaveOperation operation, std::string &error) {
  Impl::Admission admission(*impl_, error);
  if (!admission) return false;
  error.clear();
  if (!id_valid(id)) { error="application_identity_invalid"; return false; }
  if (operation == SaveOperation::Reset) return reset_save(id,error);
  const auto path="/saves/"+id+".lsave";
  std::vector<uint8_t> data;
  if (operation == SaveOperation::Export) {
    if (!export_save(id,data,error)) return false;
    if (!storage_ensure_directory("/saves") || !storage_write_file(path.c_str(),data.data(),data.size())) {
      error="备份写入失败"; return false;
    }
    return true;
  }
  struct BackupReader { std::vector<uint8_t> &data; bool oversized = false; } reader{data};
  if (!storage_stream_file(path.c_str(), [](const uint8_t *chunk, size_t size, void *p) {
    auto &reader = *static_cast<BackupReader *>(p);
    if (size > kMaximumSaveFileBytes - reader.data.size()) { reader.oversized = true; return false; }
    reader.data.insert(reader.data.end(),chunk,chunk+size); return true;
  }, &reader, &error)) {
    error=reader.oversized?"备份超过大小限制":"备份读取失败"; return false;
  }
  return import_save(id,data.data(),data.size(),error);
}
}
