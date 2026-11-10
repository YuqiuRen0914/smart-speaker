#include "lunaros_app_store.h"
#include "app/execution/application_backend.h"
#include "app/package/application_package_policy.h"
#include "lunaros_applications.h"
#include "app/package/application_limits.h"
#include "persistence/application_manifest_json.h"
#include "persistence/application_artifact_json.h"
#include "app/package/package_trust.h"
#include "lunaros_storage.h"
#include "lunaros_network_http_client.h"
#include "lunaros_network_jobs.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace lunaredu::lunaros::apps {
struct Store::Impl {
  StoreSnapshot view;
  std::string base = "https://gateway.lunaredu.cn";
  ApplicationsService *applications = nullptr;
  bool catalog_observed = false;uint64_t installed_revision = 0;
  struct Operation {
    std::string base, error;
    Application selected;
    ApplicationsService *applications = nullptr;
    std::vector<Application> result;
    ApplicationCatalogSnapshot committed;
    ApplicationOperation admission;
    bool installing = false, updating = false;
    std::atomic<bool> done{false};
    std::atomic<StorePhase> phase{StorePhase::Idle};
    std::atomic<uint32_t> received{0};
    bool ok = false;
  };
  std::unique_ptr<Operation> operation;
  ApplicationOperation uninstall;
  std::string uninstall_id;
  NetworkJobHandle job;
  static NetworkJobRunResult run(NetworkJobHandle handle, void *context) {
    auto &op = *static_cast<Operation *>(context);
    bool ok = false;
    auto fetch = [&](const std::string &path, size_t limit,
                     NetworkResponse &response) {
      NetworkFetchRequest request;
      request.url = op.base + path;
      request.timeout_ms = 10000;
      request.maximum_response_bytes = limit;
      request.transport_security =
          NetworkTransportSecurity::RequireTls;
      return network_fetch_resource(std::move(request), &response) &&
             response.status_code == 200 &&
             !network_job_cancellation_requested(handle);
    };
    if (!op.installing) {
      NetworkResponse response;
      op.phase = StorePhase::Fetching;
      if (!fetch("/api/v1/apps?schema=v2", 16384, response))
        op.error = "商店暂时无法连接";
      else {
        JsonDocument doc;
        if (deserializeJson(doc, response.body) ||
            strcmp(doc["schema"] | "", "lunaros.store/v2") ||
            !doc["apps"].is<JsonArray>() || doc["apps"].size() > kMaximumApplications)
          op.error = "商店目录无效";
        else {
          ok = true;
          for (JsonVariantConst item : doc["apps"].as<JsonArrayConst>()) {
            Application app;
            if (!decode_application_manifest(item["application"], app, op.error)) {
              ok = false;
              break;
            }
            if (!select_release_artifact(item, app,
                application_backend_available("wasm"), application_backend_available("micropython"), op.error,
                application_backend_available("aot"))) {
              // Unsupported releases do not hide unrelated usable apps.
              op.error.clear(); continue;
            }
            if (app.entry_format=="aot" && !verify_aot_package_signature(app,op.error)) {
              if (!select_release_artifact(item,app,application_backend_available("wasm"),
                  application_backend_available("micropython"),op.error,false)) {
                op.error.clear(); continue;
              }
            }
            op.result.push_back(std::move(app));
          }
        }
      }
    } else {
      // The existing Network runner supplies bytes synchronously. The OS owns
      // admission/ID reservation across this download and the actual commit.
      struct Acquisition {
        decltype(fetch) &fetcher;Operation &op;NetworkJobHandle handle;NetworkResponse response;
      } acquisition{fetch,op,handle,{}};
      const auto acquire=[](void *p,PackageBytes &bytes,std::string &error) {
        auto &a=*static_cast<Acquisition *>(p);
        a.op.phase=StorePhase::Downloading;
        if(network_job_cancellation_requested(a.handle)){error="安装已取消";return false;}
        if(!storage_ready()){error="存储尚未准备好";return false;}
        if(!a.fetcher(a.op.selected.package_url,kMaximumPackageBytes,a.response)){error="下载失败，请重试";return false;}
        a.op.received=uint32_t(a.response.body.size());a.op.phase=StorePhase::Verifying;
        if(network_job_cancellation_requested(a.handle)){error="安装已取消";return false;}
        bytes=PackageBytes(reinterpret_cast<const uint8_t *>(a.response.body.data()),a.response.body.size());
        a.op.phase=StorePhase::Committing;return true;
      };
      ok=op.applications->install_acquired(op.admission,op.selected,acquire,&acquisition,op.committed,op.error);
    }
    op.ok = ok;
    op.done.store(true, std::memory_order_release);
    return {ok, false, ok ? "app_store_complete" : "app_store_failed"};
  }
  bool submit(bool installing, const Application *app) {
    if (!applications || operation || uninstall || network_job_active(job))
      return false;
    operation = std::make_unique<Operation>();
    operation->base = base;
    operation->applications = applications;
    operation->installing = installing;
    operation->updating=app&&std::any_of(view.installed.begin(),view.installed.end(),[&](const auto &entry){return entry.id==app->id;});
    if (app)
      operation->selected = *app;
    if(installing&&!applications->request_install(app->id,operation->admission,view.error)){
      operation.reset();view.phase=StorePhase::Failed;++view.generation;return false;
    }
    NetworkJobDefinition definition;
    definition.owner_service_id = "os.appstore";
    definition.kind = installing ? "app.install" : "app.catalog";
    definition.priority = kNetworkPriorityInteractive;
    definition.runner = run;
    definition.context = operation.get();
    std::string error;
    if (!submit_network_job(definition, job, error)) {
      if(operation->admission){std::string ignored;(void)applications->cancel_install(operation->admission,ignored);}
      operation.reset();
      view.phase = StorePhase::Failed;
      view.error = "网络服务忙，请重试";
      view.generation++;
      return false;
    }
    view.phase = installing ? StorePhase::Downloading : StorePhase::Fetching;
    view.error.clear();
    view.message.clear();
    view.received_bytes = 0;
    view.total_bytes = app ? app->size : 0;
    view.generation++;
    return true;
  }
};
Store::Store() : impl_(new Impl) {}
Store::~Store() {
  if (impl_->operation) {
    std::string ignored;
    (void)cancel_network_job(impl_->job, ignored);
    // The runner owns bounded I/O timeouts. Keep its context alive until it
    // has returned, including destruction during an in-flight download.
    while (network_job_active(impl_->job))
      vTaskDelay(1);
    if(impl_->operation->admission)(void)impl_->applications->cancel_install(impl_->operation->admission,ignored);
  }
}
void Store::setup(ApplicationsService &applications) {
  auto &s = *impl_;
  s.applications = &applications;
  s.catalog_observed=false;
  s.view.installed = applications.catalog_snapshot().application_list();
  std::string endpoint;
  if (storage_read_text_file("/managed/apps/endpoint.txt", endpoint)) {
    if (network_transport_security_allows(endpoint, NetworkTransportSecurity::RequireTls)) {
      s.base = endpoint;
    } else {
      s.base.clear();
      s.view.error = "服务地址须使用 HTTPS";
    }
  }
  s.view.generation++;
}
bool Store::refresh() {
  auto &s=*impl_;
  if (s.operation || s.uninstall || network_job_active(s.job)) return false;
  return s.submit(false,nullptr);
}
bool Store::install(const std::string &id) {
  auto &s=*impl_;
  if(s.operation || s.uninstall || network_job_active(s.job))return false;
  auto candidate=std::find_if(s.view.discovered.begin(),s.view.discovered.end(),[&](const auto &a){return a.id==id;});
  if(candidate==s.view.discovered.end())return false;
  auto installed=std::find_if(s.view.installed.begin(),s.view.installed.end(),[&](const auto &a){return a.id==id;});
  if(installed!=s.view.installed.end() && compare_app_versions(candidate->version,installed->version)<=0){
    s.view.error="已是当前版本";s.view.phase=StorePhase::Failed;++s.view.generation;return false;
  }
  if(s.applications->running_id()==id){s.view.error="请先退出应用再更新";++s.view.generation;return false;}
  return s.submit(true,&*candidate);
}
bool Store::update(const std::string &id) {
  if(std::none_of(impl_->view.installed.begin(),impl_->view.installed.end(),[&](const auto &a){return a.id==id;}))return false;
  return install(id);
}
bool Store::uninstall(const std::string &id,bool data,std::string &error) {
  auto &s=*impl_;error.clear();
  if(s.operation||s.uninstall||network_job_active(s.job)){error="package_operation_pending";return false;}
  if(!s.applications||!s.applications->request_uninstall(id,data,s.uninstall,error))return false;
  s.uninstall_id=id;s.view.phase=StorePhase::Committing;
  s.view.received_bytes=s.view.total_bytes=0;
  s.view.error.clear();s.view.message="卸载已受理";++s.view.generation;return true;
}
bool Store::poll() {
  auto &s = *impl_;
  (void)s.applications->observe_catalog();
  const auto &installed = s.applications->installed();
  const auto revision=s.applications->installed_revision();
  bool catalog_changed=false;
  if(!s.catalog_observed||s.installed_revision!=revision){
    catalog_changed=installed.size()!=s.view.installed.size() ||
        !std::equal(installed.begin(),installed.end(),s.view.installed.begin(),
          [](const Application &a,const Application &b){return a.id==b.id&&a.version==b.version&&a.sha256==b.sha256;});
    s.catalog_observed=true;s.installed_revision=revision;
    // Adopt even an equivalent publication, sharing the OS-owned array.
    s.view.installed=s.applications->catalog_snapshot().application_list();
  }
  if (catalog_changed) ++s.view.generation;
  if(s.uninstall) {
    const auto result=s.uninstall.result();
    if(result.phase==ApplicationOperationPhase::Pending)return catalog_changed;
    const bool ok=result.phase==ApplicationOperationPhase::Succeeded;
    if(ok&&s.applications->ready()&&revision<result.catalog_revision)return catalog_changed;
    const bool absent=std::none_of(installed.begin(),installed.end(),[&](const auto &app){return app.id==s.uninstall_id;});
    const bool observed=ok&&s.applications->ready()&&absent;
    s.view.phase=observed?StorePhase::Idle:StorePhase::Failed;
    s.view.error=observed?"":!ok?result.error:!s.applications->ready()?"applications_service_unavailable":"installed_catalog_changed";
    s.view.message=observed?"应用已卸载":ok?"目录已提交，当前结果待核对":"卸载未完成";
    s.uninstall={};s.uninstall_id.clear();++s.view.generation;return true;
  }
  if (!s.operation) return catalog_changed;
  auto &op = *s.operation;
  if(!network_job_active(s.job)&&!op.done.load(std::memory_order_acquire)){
    // A queued job can retire without ever invoking its runner (cancel, stop or
    // dispatch deadline). Release only the unstarted request; never replay it.
    std::string ignored;
    if(op.admission)(void)s.applications->cancel_install(op.admission,ignored);
    s.view.phase=StorePhase::Failed;s.view.error="application_acquisition_not_completed";
    s.view.message="请求未完成，请检查后重试";s.operation.reset();s.job={};++s.view.generation;return true;
  }
  bool changed = false;
  auto phase = op.phase.load();
  if (phase != StorePhase::Idle && s.view.phase != phase) {
    s.view.phase = phase;
    changed = true;
  }
  auto received = op.received.load();
  if (received != s.view.received_bytes) {
    s.view.received_bytes = received;
    changed = true;
  }
  if (op.done.load(std::memory_order_acquire) && !network_job_active(s.job)) {
    // A commit may be publishing on another I/O owner. Keep the receipt until
    // Main has observed at least its revision; never reread storage here.
    if (op.ok && op.installing && op.committed && s.applications->ready() &&
        s.applications->installed_revision() < op.committed.revision()) {
      if (changed) ++s.view.generation;
      return changed || catalog_changed;
    }
    if (op.ok) {
      s.view.phase = StorePhase::Idle;
      s.view.error.clear();
      if (op.installing) {
        if (!s.applications->ready()) {
          s.view.phase=StorePhase::Failed;
          s.view.message="应用已保存，目录观察暂不可用";
          s.view.error="applications_service_unavailable";
        } else if (op.committed) {
          s.view.installed = s.applications->catalog_snapshot().application_list();
          const bool present=std::any_of(s.view.installed.begin(),s.view.installed.end(),[&](const auto &app){return app.id==op.selected.id&&app.version==op.selected.version;});
          if(present){
            s.view.message = op.updating?"更新完成":"安装完成";
            if (!op.error.empty()) s.view.message += "，文件待清理";
          }else{
            s.view.phase=StorePhase::Failed;s.view.message="应用已保存，目录已被其他操作修改";
            s.view.error="installed_catalog_changed";
          }
        } else {
          s.view.phase = StorePhase::Failed;
          s.view.message = "应用已保存，目录发布未确认";
          s.view.error = "installed_catalog_receipt_missing";
        }
      } else
        s.view.discovered = ApplicationList(std::move(op.result));
    } else {
      s.view.phase = StorePhase::Failed;
      s.view.error = op.error;
    }
    s.operation.reset();
    s.job = {};
    changed = true;
  }
  if (changed)
    s.view.generation++;
  return changed || catalog_changed;
}
const StoreSnapshot &Store::snapshot() const { return impl_->view; }
const std::string &Store::endpoint() const { return impl_->base; }
bool Store::set_endpoint(const std::string &url) {
  if (impl_->operation || impl_->uninstall || url.empty() || url.size() > 180 ||
      url.back() == '/' ||
      !network_transport_security_allows(
          url, NetworkTransportSecurity::RequireTls) ||
      !storage_write_text_file("/managed/apps/endpoint.txt", url.c_str()))
    return false;
  impl_->base = url;
  impl_->view.discovered = {};
  impl_->view.generation++;
  return true;
}
} // namespace lunaredu::lunaros::apps
