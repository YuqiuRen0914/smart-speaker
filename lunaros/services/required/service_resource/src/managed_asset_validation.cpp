#include "lunaros_resource_managed_assets.h"
#include <array>
namespace lunaredu::lunaros {
bool validate_managed_assets(const char *const *ids,size_t count,
    std::string &failed_asset,std::string &error) {
  failed_asset.clear();error.clear();
  if((!ids&&count)||count>kManagedAssetCapacity){error="asset_set_invalid";return false;}
  std::array<uint32_t,kManagedAssetCapacity> generations{};
  for(size_t i=0;i<count;++i){
    if(!ids[i]||!*ids[i]){error="asset_id_invalid";return false;}
    generations[i]=managed_asset_generation(ids[i]);
    if(!validate_managed_asset(ids[i],error)){failed_asset=ids[i];return false;}
  }
  for(size_t i=0;i<count;++i){
    if(generations[i]!=managed_asset_generation(ids[i])){
      failed_asset=ids[i];error="asset_publication_changed";return false;
    }
  }
  return true;
}
}
