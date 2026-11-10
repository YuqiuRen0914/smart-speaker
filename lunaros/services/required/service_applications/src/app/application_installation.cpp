#include "app/application_installation.h"
#include "app/package/application_limits.h"
#include "lunaros_storage.h"
#include "app/application_persistence.h"
#include <algorithm>
#include <cstdio>
namespace lunaredu::lunaros::apps {
namespace {
constexpr char index_path[] = "/managed/apps/catalog.json";

}
bool load_installed_applications(std::vector<Application> &apps) {
  std::string text;if(!storage_read_text_file_bounded(index_path,kMaximumCatalogBytes,text))return false;
  std::vector<Application> next;
  if (!decode_installed_catalog(text, next)) return false;
  std::vector<Application> validated;
  for(auto &a : next){
    if(a.package_version!=kApplicationPlatform.package_version)return false;
    if(!application_backend_contract(a.backend))return false;
    const bool aot=a.backend=="wasm" && a.entry_format=="aot";
    if(a.entry_format!=a.backend && !aot)return false;
    if(a.artifact_id.empty()||a.artifact_id.size()>64||
       !digest_valid(a.source_sha256)||!digest_valid(a.entry_sha256)||
       (!aot && a.source_sha256!=a.entry_sha256))return false;
    if(aot && (a.signer_id.empty() || a.signer_id.size()>64 ||
       a.package_signature.size()!=128 || !digest_valid(a.package_signature.substr(0,64)) ||
       !digest_valid(a.package_signature.substr(64))))return false;
    a.installed=true;
    unsigned parts[3]{};
    if(!id_valid(a.id)||!version_parts(a.version,parts)||a.name.empty()||a.name.size()>kApplicationPlatform.maximum_name_bytes||a.sha256.size()!=64||a.size>kMaximumPackageBytes||a.tick_hz<0||a.tick_hz>kApplicationPlatform.maximum_tick_hz||a.icon_resource.size()>kApplicationPlatform.maximum_icon_resource_bytes||validated.size()>=kMaximumApplications||!storage_path_exists(application_package_path(a).c_str())||std::any_of(validated.begin(),validated.end(),[&](const auto &x){return x.id==a.id;}))return false;
    if(!digest_valid(a.sha256))return false;
    validated.push_back(std::move(a));
  }
  apps=std::move(validated);return true;
}
bool commit_installed_applications(const std::vector<Application> &apps) {
  std::string text;
  return encode_installed_catalog(apps,text) && storage_write_text_file(index_path,text.c_str());
}
bool collect_application_packages(const std::string &id,const std::string &keep) {
  if(!id_valid(id))return false;
  const auto dir="/managed/apps/"+id;std::vector<std::string> paths;
  if(!storage_path_exists(dir.c_str()))return true;
  if(!storage_list_entries(dir.c_str(),[](const StorageListEntry &entry,void *context){if(!entry.is_directory)static_cast<std::vector<std::string> *>(context)->push_back(entry.path);return true;},&paths))return false;
  bool ok=true;for(const auto &path:paths){if(path.rfind(dir+"/",0)!=0)continue;const auto file=path.substr(dir.size()+1);unsigned parts[3]{};if(path!=keep&&file.size()>5&&file.substr(file.size()-5)==".lapp"&&version_parts(file.substr(0,file.size()-5),parts))ok=storage_delete_file(path.c_str())&&ok;}return ok;
}
bool remove_installed_application(std::vector<Application> &installed,const std::string &id,bool delete_data,std::string &error) {
  error.clear();if(!id_valid(id)||std::none_of(installed.begin(),installed.end(),[&](const auto &a){return a.id==id;})){error="应用未安装";return false;}
  auto next=installed;next.erase(std::remove_if(next.begin(),next.end(),[&](const auto &a){return a.id==id;}),next.end());
  const auto marker="/managed/apps/"+id+"/delete-data.pending";
  // Persist deletion intent before publishing removal. Collection discards an
  // aborted intent while the application remains installed.
  if (delete_data && !storage_write_text_file(marker.c_str(),"1")) { error="卸载清理记录写入失败"; return false; }
  if (!delete_data && storage_path_exists(marker.c_str()) && !storage_delete_file(marker.c_str())) {
    error="卸载清理记录移除失败"; return false;
  }
  // Publish removal first. A failed index write must preserve packages and data.
  if(!commit_installed_applications(next)){if(delete_data)(void)storage_delete_file(marker.c_str());error="卸载目录提交失败";return false;}
  installed=std::move(next);
  error="application_cleanup_pending";
  return true;
}
bool finish_application_data_deletion(const std::string &id, bool installed) {
  if (!id_valid(id)) return false;
  const auto marker="/managed/apps/"+id+"/delete-data.pending";
  if (!storage_path_exists(marker.c_str())) return true;
  if (!installed) {
    const auto path="/managed/apps/"+id+"/save.bin";
    bool ok=true;
    for(const auto &file:{path,path+".meta"})
      if(storage_path_exists(file.c_str())) ok=storage_delete_file(file.c_str())&&ok;
    if(!ok)return false;
  }
  return storage_delete_file(marker.c_str());
}
bool collect_unreferenced_application_packages(const std::vector<Application> &installed) {
  // Products without installed Apps need no managed-app directory yet.
  if (!storage_path_exists("/managed/apps")) return true;
  std::vector<std::string> ids;
  if(!storage_list_entries("/managed/apps",[](const StorageListEntry &entry,void *context){
    const std::string prefix="/managed/apps/";
    if(entry.is_directory&&entry.path.rfind(prefix,0)==0){const auto id=entry.path.substr(prefix.size());if(id_valid(id))static_cast<std::vector<std::string> *>(context)->push_back(id);}
    return true;
  },&ids))return false;
  bool ok=true;for(const auto &id:ids){
    auto app=std::find_if(installed.begin(),installed.end(),[&](const auto &a){return a.id==id;});
    bool collected=collect_application_packages(id,app==installed.end()?"":application_package_path(*app));
    const auto marker="/managed/apps/"+id+"/delete-data.pending";
    if(storage_path_exists(marker.c_str())) collected=finish_application_data_deletion(id,app!=installed.end())&&collected;
    ok=collected&&ok;
  }return ok;
}
}
