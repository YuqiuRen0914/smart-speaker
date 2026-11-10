#include "lunaros_settings_account_avatar.h"
#include "lunaros_resource.h"
#include <cstdlib>
#include <cmath>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
namespace lunaredu::settings {
namespace {
constexpr size_t bytes=96*96*3;
struct State {
  lunaros::ResourceReadOperation read;
  std::string owner,revision,loaded_revision,read_owner;
  int epoch=0,read_epoch=0;
  uint32_t next_attempt=0;
  uint64_t generation=0;
  uint8_t *buffer=nullptr;
  std::shared_ptr<const desktop::DesktopImage> image;
};
State state;
const std::shared_ptr<const desktop::DesktopImage> &placeholder(){
  static const auto image=[] {
    auto value=std::make_shared<desktop::DesktopImage>();
    value->width=value->height=32;value->pixels.resize(32*32,0x245f);
    for(int y=0;y<32;++y)for(int x=0;x<32;++x)
      if(std::hypot(float(x)-15.5f,float(y)-10.f)<5.f||(y>=19&&std::hypot(float(x)-15.5f,(float(y)-29.f)*.8f)<11.f))value->pixels[y*32+x]=0xffff;
    return std::shared_ptr<const desktop::DesktopImage>(value);
  }();return image;
}
bool due(uint32_t now){return int32_t(now-state.next_attempt)>=0;}
}
std::shared_ptr<const desktop::DesktopImage> observe_account_avatar(const AccountAvatarRequest &request,uint32_t now){
  return observe_account_avatar_view({request.bound,request.available,request.account,request.revision,request.url,request.format,request.epoch,request.width,request.height},now);
}
std::shared_ptr<const desktop::DesktopImage> observe_account_avatar_view(const AccountAvatarView &request,uint32_t now){
  using namespace lunaros;
  const bool bound=request.bound&&!request.account.empty();
  const bool changed=!bound||state.owner!=request.account||state.epoch!=request.epoch||state.revision!=request.revision||!request.available;
  if(changed){
    state.image.reset();state.loaded_revision.clear();state.generation=0;
    if(state.owner!=request.account||state.epoch!=request.epoch||state.revision!=request.revision)state.next_attempt=now;
    state.owner=bound?request.account:"";state.epoch=request.epoch;state.revision=request.revision;
  }
  const auto completion=resource_read_state(state.read);
  if(completion==ResourceReadState::Pending)return bound?(state.image?state.image:placeholder()):nullptr;
  if(completion!=ResourceReadState::Idle){
    if(completion==ResourceReadState::Succeeded&&bound&&request.available&&state.read.revision==request.revision&&state.read.size==bytes&&
       resource_publication_current("account-avatar",state.read.generation)&&state.read_owner==request.account&&state.read_epoch==request.epoch&&!changed){
      auto image=std::make_shared<desktop::DesktopImage>();image->width=image->height=32;image->pixels.resize(32*32);
      for(size_t y=0;y<32;++y)for(size_t x=0;x<32;++x){
        unsigned rgb[3]{};
        for(size_t dy=0;dy<3;++dy)for(size_t dx=0;dx<3;++dx)
          for(size_t channel=0;channel<3;++channel)rgb[channel]+=state.buffer[((y*3+dy)*96+x*3+dx)*3+channel]&63;
        image->pixels[y*32+x]=uint16_t(((rgb[0]/9)>>1)<<11|(rgb[1]/9)<<5|((rgb[2]/9)>>1));
      }
      state.image=std::move(image);state.loaded_revision=request.revision;state.generation=state.read.generation;
    }
    state.read.content=nullptr;state.read.capacity=state.read.size=0;
    std::free(state.buffer);state.buffer=nullptr;
    state.read.state.store(ResourceReadState::Idle,std::memory_order_release);
  }
  if(!bound)return nullptr;
  if(!request.available||request.format!="rgb666-u8x3"||request.width!=96||request.height!=96||request.revision.empty()||request.url.empty())return placeholder();
  if(state.image&&state.loaded_revision==request.revision&&resource_publication_current("account-avatar",state.generation))return state.image;
  state.image.reset();
  if(!due(now))return placeholder();
  state.next_attempt=now+2000;
  const auto resource=resource_snapshot("account-avatar");
  if(resource.revision!=request.revision||resource.state==ResourceState::Missing||resource.state==ResourceState::Error){
    ResourceDescriptor descriptor;descriptor.id="account-avatar";descriptor.revision=request.revision;descriptor.url=request.url;
    descriptor.format=request.format;descriptor.width=descriptor.height=96;descriptor.maximum_bytes=bytes;descriptor.scope=ResourceScope::AccountPrivate;
    (void)request_resource(std::move(descriptor),20);return placeholder();
  }
  if(resource.state!=ResourceState::Ready||resource.size_bytes!=bytes||resource.format!=request.format||resource.width!=96||resource.height!=96)return placeholder();
  if(!state.buffer){
#ifdef ESP_PLATFORM
    state.buffer=static_cast<uint8_t *>(heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#else
    state.buffer=static_cast<uint8_t *>(std::malloc(bytes));
#endif
  }
  if(!state.buffer)return placeholder();
  state.read.id="account-avatar";state.read.revision=request.revision;state.read.content=state.buffer;state.read.capacity=bytes;
  state.read_owner=request.account;state.read_epoch=request.epoch;
  if(!schedule_resource_read(state.read)){
    state.read.content=nullptr;state.read.capacity=state.read.size=0;
    std::free(state.buffer);state.buffer=nullptr;
  }
  return placeholder();
}
}
