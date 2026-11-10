#pragma once
#include "lunaros_resource_managed_assets.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <array>
#include <utility>

namespace lunaredu::lunaros {
struct ManagedAssetInstallTarget {
  std::string revision,sha256,url;
  size_t bytes=0;
  bool operator==(const ManagedAssetInstallTarget &) const=default;
};
// A bounded target description. Resource owns manifest/content matching; the
// OTA installation owner supplies authentication, ELF intent and activation.
// This helper never downloads, publishes, confirms firmware or erases a plan.
template<size_t Count> class ManagedAssetInstallPlan {
public:
  static_assert(Count>0&&Count<=kManagedAssetCapacity);
  using Targets=std::array<ManagedAssetInstallTarget,Count>;
  struct Observation {bool ready=false,downloading=false;};
  explicit constexpr ManagedAssetInstallPlan(std::array<const char *,Count> ids,size_t count=Count):ids_(ids),count_(count<=Count?count:Count){}
  bool parse(JsonVariantConst input,Targets &output) const {
    if(!input.is<JsonArrayConst>())return false;
    Targets next;std::array<bool,Count> seen{};
    for(JsonVariantConst item:input.as<JsonArrayConst>()){
      const std::string id=item["id"]|"";
      for(size_t i=0;i<count_;++i)if(id==ids_[i]){
        if(seen[i]||!item["bytes"].is<size_t>())return false;
        seen[i]=true;next[i]={item["revision"]|"",item["sha256"]|"",item["url"]|"",item["bytes"].as<size_t>()};
      }
    }
    for(size_t i=0;i<count_;++i){
      const auto &t=next[i];
      if(!seen[i]||t.revision.empty()||t.revision.size()>64||t.bytes<16||
          t.sha256.size()!=64||!std::all_of(t.sha256.begin(),t.sha256.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');})||
          t.url.size()>1024||!t.url.starts_with("https://")||t.url.size()<=8||t.url.find_first_of("\r\n\t ")!=std::string::npos)return false;
    }
    output=std::move(next);return true;
  }
  void encode(JsonArray output,const Targets &targets) const {
    for(size_t i=0;i<count_;++i){auto item=output.add<JsonObject>();const auto &t=targets[i];
      item["id"]=ids_[i];item["revision"]=t.revision;item["sha256"]=t.sha256;item["url"]=t.url;item["bytes"]=t.bytes;}
  }
  bool same_content(const Targets &a,const Targets &b) const {
    for(size_t i=0;i<count_;++i)if(a[i].revision!=b[i].revision||a[i].sha256!=b[i].sha256||a[i].bytes!=b[i].bytes)return false;
    return true; // Rotating a signed URL does not select different content.
  }
  Observation observe(size_t index,const ManagedAssetInventorySnapshot &inventory,const Targets &targets) const {
    if(index>=count_)return {};
    const auto &t=targets[index];
    for(const auto &a:inventory.assets)if(a.id==ids_[index])
      return {a.state==ManagedAssetState::Ready&&a.revision==t.revision&&a.sha256==t.sha256&&a.bytes==t.bytes,a.downloading};
    return {};
  }
  bool matches(const ManagedAssetInventorySnapshot &inventory,const Targets &targets) const {
    for(size_t i=0;i<count_;++i){const auto state=observe(i,inventory,targets);if(!state.ready||state.downloading)return false;}
    return true;
  }
  ManagedAssetRequest request(size_t index,const Targets &targets) const {
    if(index>=count_)return {};
    const auto &t=targets[index];return {ids_[index],t.revision,t.url,t.sha256,t.bytes};
  }
private:
  std::array<const char *,Count> ids_;
  size_t count_;
};
}
