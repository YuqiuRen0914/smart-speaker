#include "lunaredu_pet_ui.h"
#include "app/caption.h"
#include <algorithm>
#include <utility>
#include <tuple>

namespace lunaredu::pet {
bool VoiceView::operator==(const VoiceView &v) const {return std::tie(state,action,status,caption,caption_audio_clock,caption_character)==std::tie(v.state,v.action,v.status,v.caption,v.caption_audio_clock,v.caption_character);}
bool ActivityView::operator==(const ActivityView &v) const {return std::tie(id,title,status,detail,cancellable)==std::tie(v.id,v.title,v.status,v.detail,v.cancellable);}
bool ConnectionsView::operator==(const ConnectionsView &v) const {
    // Authority revisions are consumed by commands, not visible pixels.
    return std::tie(identity_scope,device_status,voice,activities,total,offset,count,items,selected,activity_status,management_url,media)==
      std::tie(v.identity_scope,v.device_status,v.voice,v.activities,v.total,v.offset,v.count,v.items,v.selected,v.activity_status,v.management_url,v.media);
  }

namespace {
bool same_targets(const ConnectionsView &a,const ConnectionsView &b) {
  if(a.media.state!=b.media.state||a.media.token!=b.media.token||a.media.playing!=b.media.playing||a.media.paused!=b.media.paused||
     a.identity_scope!=b.identity_scope || a.voice.state!=b.voice.state ||
     a.voice.action!=b.voice.action || a.voice.action_token!=b.voice.action_token || a.activities!=b.activities ||
     a.configuration_revision!=b.configuration_revision || a.offset!=b.offset ||
     a.total!=b.total || a.count!=b.count || a.selected.id!=b.selected.id ||
     a.selected.cancellable!=b.selected.cancellable)return false;
  for(size_t i=0;i<a.count;++i)if(a.items[i].id!=b.items[i].id)return false;
  return true;
}
}
void UiSession::invalidate(){++input_revision_;if(!input_revision_)++input_revision_;++presentation_revision_;}
void UiSession::feature_support(uint8_t bits){if(view_.feature_support!=bits){view_.feature_support=bits;invalidate();}}
void UiSession::enter(DisplayMode mode){leave();active_=true;view_.mode=mode;view_.features=feature_preferences();}
void UiSession::leave(){active_=false;view_={};next_poll_=0;selected_id_.clear();caption_.clear();caption_started_ms_=0;confirmation_revision_=0;invalidate();}
void UiSession::message(std::string text){view_.message=std::move(text);view_.caption_page=0;invalidate();}
void UiSession::setup_caption(const std::string &text){if(view_.setup_caption!=text){view_.setup_caption=text;++presentation_revision_;}}
void UiSession::refresh(const ConnectionPorts &ports){
  ConnectionsView next;
  if(ports.read && !ports.read(ports.context,view_.connections.offset,selected_id_.c_str(),next)){
    next={};next.voice.state=ConnectionState::Unavailable;next.activities=ConnectionState::Unavailable;
    next.activity_status="连接暂不可用";
  }
  next.count=std::min(next.count,next.items.size());
  if(next.voice.state!=ConnectionState::Ready)next.voice.action=VoiceAction::None;
  if(next.activities!=ConnectionState::Ready){next.items={};next.selected={};next.total=next.count=next.offset=0;}
  const bool rebinding=next.identity_scope!=view_.connections.identity_scope;
  if(rebinding){
    selected_id_.clear();view_.confirming=false;view_.detail_page=0;view_.message.clear();
    if(view_.page==Page::Activity)view_.page=Page::Activities;
    // Don't display a selection from the previous account for even one frame.
    next.selected={};
  }
  if(!same_targets(view_.connections,next))invalidate();
  else if(!(view_.connections==next))++presentation_revision_;
  view_.connections=std::move(next);
  if(view_.page==Page::Activity && view_.connections.selected.id.empty()){
    view_.page=Page::Activities;view_.confirming=false;selected_id_.clear();invalidate();
  }
}
void UiSession::poll(const ConnectionPorts &ports,uint64_t now,bool force){
  if(!active_)return;
  const auto features=feature_preferences();
  if(features.bits()!=view_.features.bits()){view_.features=features;invalidate();}
  if(!force && now<next_poll_)return;
  next_poll_=now+500;refresh(ports);
  const auto &caption=home_caption(view_);
  if(caption!=caption_){caption_=caption;caption_started_ms_=now;view_.caption_page=0;++presentation_revision_;}
  else if(!caption.empty() && !view_.connections.voice.caption_audio_clock){
    const auto page=(now>=caption_started_ms_?now-caption_started_ms_:0)/3000;
    if(page!=view_.caption_page){view_.caption_page=page;++presentation_revision_;}
  }
}
bool UiSession::send(const ConnectionPorts &ports,ConnectionCommand command){
  if(!ports.command){message("操作暂不可用");return false;}
  const auto receipt=ports.command(ports.context,command);
  message(receipt.message.empty()?receipt.state==CommandState::Accepted?"已提交，请等待最终结果":
    receipt.state==CommandState::Confirmed?"操作已完成":receipt.state==CommandState::Unknown?"结果待核对，请查看工作台":"状态已变化，请重新核对":receipt.message);
  refresh(ports);invalidate();
  return receipt.state==CommandState::Accepted || receipt.state==CommandState::Confirmed;
}
bool UiSession::action(UiAction action,const ConnectionPorts &ports){
  if(!active_)return false;
  if(action>=UiAction::MediaToggle&&action<=UiAction::MediaOpen){
    const auto before=view_.connections.media;refresh(ports);
    if(before.state!=ConnectionState::Ready||view_.connections.media.state!=ConnectionState::Ready||
       before.token!=view_.connections.media.token||before.playing!=view_.connections.media.playing||before.paused!=view_.connections.media.paused){
      message("音乐状态已变化，请重试");return false;
    }
    ConnectionCommand command;command.kind=ConnectionCommandKind::Media;
    command.media=action==UiAction::MediaToggle?MediaAction::Toggle:action==UiAction::MediaPrevious?MediaAction::Previous:
        action==UiAction::MediaNext?MediaAction::Next:action==UiAction::MediaStop?MediaAction::Stop:MediaAction::Open;
    command.media_token=before.token;
    const bool okay=send(ports,std::move(command));
    if(okay)message(""); // Playback facts/lyrics replace successful command receipts.
    return okay;
  }
  if(action==UiAction::Voice){
    const auto before=view_.connections;
    refresh(ports);
    if(before.identity_scope!=view_.connections.identity_scope || before.voice.action!=view_.connections.voice.action ||
       before.voice.action_token!=view_.connections.voice.action_token ||
       view_.connections.voice.state!=ConnectionState::Ready || before.voice.action==VoiceAction::None){message("语音状态已变化，请重试");return false;}
    return send(ports,{ConnectionCommandKind::Voice,before.voice.action,before.identity_scope,{},before.revision,before.voice.action_token});
  }
  if(action==UiAction::ConfirmCancel && view_.confirming){
    const auto before=view_.connections;view_.confirming=false;
    refresh(ports);
    if(before.identity_scope!=view_.connections.identity_scope || before.selected.id!=view_.connections.selected.id ||
       !view_.connections.selected.cancellable || before.configuration_revision!=view_.connections.configuration_revision){message("任务已变化，请重新核对");invalidate();return false;}
    return send(ports,{ConnectionCommandKind::CancelActivity,VoiceAction::None,before.identity_scope,before.selected.id,confirmation_revision_});
  }
  switch(action){
  case UiAction::ConnectionNext:view_.network_setup_link=!view_.network_setup_link;break;
  case UiAction::PairAccount:case UiAction::ConfigureNetwork:case UiAction::RetryConnection:case UiAction::CloseConnection:view_.network_setup_link=false;break;
  case UiAction::Home:view_.page=Page::Home;break;
  case UiAction::Activities:view_.page=Page::Activities;selected_id_.clear();view_.connections.offset=0;refresh(ports);break;
  case UiAction::Settings:view_.page=Page::Settings;break;
  case UiAction::Features:view_.page=Page::Features;break;
  case UiAction::Appearance:view_.page=Page::Appearance;break;
  case UiAction::Connections:view_.page=Page::Connections;break;
  case UiAction::ToggleMode:view_.mode=view_.mode==DisplayMode::Window?DisplayMode::Fullscreen:DisplayMode::Window;break;
  case UiAction::Previous:
    if(view_.page==Page::Activity){if(view_.detail_page)--view_.detail_page;}
    else {view_.connections.offset=view_.connections.offset>=3?view_.connections.offset-3:0;refresh(ports);}break;
  case UiAction::Next:
    if(view_.page==Page::Activity){if(view_.detail_page<15)++view_.detail_page;}
    else if(view_.connections.offset+view_.connections.count<view_.connections.total){view_.connections.offset+=3;refresh(ports);}
    break;
  case UiAction::SelectFirst:case UiAction::SelectSecond:case UiAction::SelectThird:{
    const auto index=static_cast<unsigned>(action)-static_cast<unsigned>(UiAction::SelectFirst);
    if(index>=view_.connections.count)return false;
    selected_id_=view_.connections.items[index].id;view_.page=Page::Activity;view_.detail_page=0;refresh(ports);break;}
  case UiAction::CancelActivity:
    if(view_.page!=Page::Activity || !view_.connections.selected.cancellable)return false;
    view_.confirming=true;confirmation_revision_=view_.connections.revision;invalidate();return true;
  case UiAction::DismissConfirm:view_.confirming=false;invalidate();return true;
  default:return false;
  }
  view_.message.clear();view_.confirming=false;invalidate();return true;
}
} // namespace lunaredu::pet
