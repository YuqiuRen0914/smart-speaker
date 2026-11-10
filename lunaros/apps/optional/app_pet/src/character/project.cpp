#include "character/project.h"
#include "lunaros_storage.h"
#include "lunaros_storage_private_paths.h"
#include "lunaros_digest.h"
#include "lunaros_resource.h"
#include "lunaros_platform_link.h"
#include <mutex>

namespace lunaredu::pet::character {
namespace {
constexpr const char *directory="/_lunaros_private/pet";
constexpr const char *installed_path="/_lunaros_private/pet/character-installed.json";
constexpr const char *active_path="/_lunaros_private/pet/character-active.json";
constexpr const char *resource_id="pet-character-project";
std::mutex state_mutex,write_mutex;
Project active;
std::string installed_revision,active_revision,installed_name,active_name,last_error;
std::string pending_revision,pending_scope;
int pending_epoch=0;
bool restored=false;
bool valid_hash(const std::string &hash){
  return hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==std::string::npos;
}
std::string hash_of(const std::string &json){return lunaros::digest::sha256_hex(reinterpret_cast<const uint8_t *>(json.data()),json.size());}
bool read_project(const char *path,Project &project,std::string &json,std::string &revision,std::string &error){
  if(!lunaros::storage_read_text_file_bounded(path,kMaximumProjectBytes,json)){error="character_project_read_failed";return false;}
  if(!parse_project(json,project,error))return false;
  revision=hash_of(json);if(!valid_hash(revision)){error="character_project_hash_failed";return false;}return true;
}
}
void restore(){
  std::lock_guard write(write_mutex);
  if(restored||!lunaros::storage_ready())return;
  Project candidate;std::string json,revision,error;
  if(lunaros::storage_path_exists(installed_path)){
    if(read_project(installed_path,candidate,json,revision,error)){
      std::lock_guard lock(state_mutex);installed_revision=revision;installed_name=candidate.name;
    }else{std::lock_guard lock(state_mutex);last_error=error;}
  }
  if(lunaros::storage_path_exists(active_path)){
    if(read_project(active_path,candidate,json,revision,error)){
      std::lock_guard lock(state_mutex);active=std::move(candidate);active_revision=revision;active_name=active.name;publish_expression_configs(active.expressions,active.overridden);
    }else{std::lock_guard lock(state_mutex);last_error=error;}
  }
  restored=true;
}
bool install(const std::string &json,const std::string &sha256,std::string &error){
  Project candidate;if(!parse_project(json,candidate,error))return false;
  if(!valid_hash(sha256)||hash_of(json)!=sha256){error="character_project_digest_mismatch";return false;}
  restore();std::lock_guard write(write_mutex);
  {std::lock_guard lock(state_mutex);if(!pending_revision.empty()){error="character_project_download_busy";return false;}}
  if(!lunaros::storage_ready()||!lunaros::storage_ensure_directory(directory)||
     !lunaros::storage_write_text_file(installed_path,json.c_str())){error="character_project_write_failed";return false;}
  std::lock_guard lock(state_mutex);installed_revision=sha256;installed_name=candidate.name;last_error.clear();error.clear();return true;
}
bool activate(const std::string &revision,std::string &error){
  restore();std::lock_guard write(write_mutex);
  Project candidate;std::string json,actual;
  if(!valid_hash(revision)){error="character_project_revision_invalid";return false;}
  if(!read_project(installed_path,candidate,json,actual,error))return false;
  if(actual!=revision){error="character_project_revision_changed";return false;}
  if(!lunaros::storage_write_text_file(active_path,json.c_str())){error="character_project_activation_write_failed";return false;}
  std::lock_guard lock(state_mutex);active=std::move(candidate);active_revision=revision;active_name=active.name;last_error.clear();publish_expression_configs(active.expressions,active.overridden);error.clear();return true;
}
bool remove(std::string &error){
  restore();std::lock_guard write(write_mutex);
  {std::lock_guard lock(state_mutex);
    if(!pending_revision.empty()){(void)lunaros::release_resource(resource_id,true);pending_revision.clear();pending_scope.clear();pending_epoch=0;}
  }
  // Reverting the active publication is committed before changing renderer state.
  if(lunaros::storage_path_exists(active_path)&&!lunaros::storage_delete_file(active_path)){error="character_project_remove_failed";return false;}
  {std::lock_guard lock(state_mutex);active={};active_revision.clear();active_name.clear();publish_expression_configs(active.expressions,active.overridden);}
  if(lunaros::storage_path_exists(installed_path)&&!lunaros::storage_delete_file(installed_path)){error="character_project_remove_failed";return false;}
  std::lock_guard lock(state_mutex);installed_revision.clear();installed_name.clear();last_error.clear();error.clear();return true;
}
void fill_state(JsonObject out){
  std::lock_guard lock(state_mutex);
  out["installed_revision"]=installed_revision;out["active_revision"]=active_revision;
  out["installed_name"]=installed_name;out["active_name"]=active_name;
  out["active_generation"]=active_generation();out["rendered_generation"]=rendered_generation();
  out["pending_revision"]=pending_revision;out["error"]=last_error;
  out["maximum_bytes"]=kMaximumProjectBytes;out["download_pending"]=!pending_revision.empty();
}
bool request_fetch(const std::string &url,const std::string &sha256,size_t bytes,std::string &error){
  const auto scope=lunaros::platform_link_account_scope();
  if(scope.identity.empty()){error="character_project_binding_required";return false;}
  if(url.compare(0,8,"https://")||url.size()>1536||!valid_hash(sha256)||!bytes||bytes>kMaximumProjectBytes){error="character_project_resource_invalid";return false;}
  std::lock_guard write(write_mutex);
  {std::lock_guard lock(state_mutex);if(!pending_revision.empty()){error="character_project_download_busy";return false;}}
  lunaros::ResourceDescriptor descriptor;descriptor.id=resource_id;descriptor.revision=sha256;
  descriptor.url=url;descriptor.sha256=sha256;descriptor.format="json";
  descriptor.maximum_bytes=bytes;descriptor.scope=lunaros::ResourceScope::AccountPrivate;
  if(!lunaros::request_resource(std::move(descriptor),1)){error="character_project_download_rejected";return false;}
  std::lock_guard lock(state_mutex);pending_revision=sha256;pending_scope=scope.identity;pending_epoch=scope.binding_epoch;last_error.clear();error.clear();return true;
}
void poll_fetch(){
  std::string revision,scope_key;int epoch=0;
  {std::lock_guard lock(state_mutex);revision=pending_revision;scope_key=pending_scope;epoch=pending_epoch;}
  if(revision.empty())return;
  const auto scope=lunaros::platform_link_account_scope();
  const auto resource=lunaros::resource_snapshot(resource_id);
  std::string error;
  if(scope.identity!=scope_key||scope.binding_epoch!=epoch)error="character_project_binding_changed";
  else if(resource.state==lunaros::ResourceState::Error)error=resource.error.empty()?"character_project_download_failed":resource.error;
  else if(resource.state!=lunaros::ResourceState::Ready)return;
  else{
    std::vector<uint8_t> bytes;lunaros::ResourceSnapshot publication;
    if(!lunaros::read_ready_resource(resource_id,bytes,&publication)||publication.revision!=revision||bytes.size()>kMaximumProjectBytes)
      error="character_project_resource_changed";
    else if(!lunaros::resource_publication_current(resource_id,publication.generation))error="character_project_resource_changed";
    else {
      // Hold the mutation owner through validation and publication. A local install
      // cannot replace the pending resource, and remove cancels it before returning.
      std::lock_guard write(write_mutex);
      bool current;
      {std::lock_guard lock(state_mutex);current=pending_revision==revision&&pending_scope==scope_key;}
      const auto binding=lunaros::platform_link_account_scope();
      Project candidate;const std::string json(bytes.begin(),bytes.end());
      if(!current||binding.identity!=scope_key||binding.binding_epoch!=epoch)error="character_project_binding_changed";
      else if(!parse_project(json,candidate,error)){}
      else if(hash_of(json)!=revision)error="character_project_digest_mismatch";
      else if(!lunaros::storage_ready()||!lunaros::storage_ensure_directory(directory)||!lunaros::storage_write_text_file(installed_path,json.c_str()))error="character_project_write_failed";
      else{std::lock_guard lock(state_mutex);installed_revision=revision;installed_name=candidate.name;}
    }
  }
  (void)lunaros::release_resource(resource_id,true);
  std::lock_guard lock(state_mutex);if(pending_revision==revision){pending_revision.clear();pending_scope.clear();pending_epoch=0;last_error=error;}
}
}
