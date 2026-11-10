#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace lunaredu::pet {
// Presentation facts, not service-owned journals. Product composition maps its
// chosen providers here; neither provider headers nor service lifetimes escape.
enum class ConnectionState { Absent, Unavailable, Ready };
enum class VoiceAction { None, BeginInput, FinishInput, StopReply };
struct VoiceView {
  ConnectionState state=ConnectionState::Absent;
  VoiceAction action=VoiceAction::None;
  std::string status, caption;
  std::string action_token{};
  bool caption_audio_clock = false;
  uint32_t caption_character = 0;
  bool operator==(const VoiceView &) const;
};
struct ActivityView {
  std::string id, title, status, detail;
  bool cancellable=false;
  bool operator==(const ActivityView &) const;
};
enum class MediaAction { Toggle, Previous, Next, Stop, Open };
struct MediaView {
  ConnectionState state=ConnectionState::Absent;
  bool playing=false,paused=false;
  std::string title,caption,token;
  bool operator==(const MediaView &) const=default;
};
struct ConnectionsView {
  std::string identity_scope;
  std::string device_status;
  VoiceView voice;
  ConnectionState activities=ConnectionState::Absent;
  uint64_t revision=0, configuration_revision=0;
  size_t total=0, offset=0, count=0;
  std::array<ActivityView,3> items{};
  ActivityView selected;
  std::string activity_status, management_url;
  MediaView media;
  bool operator==(const ConnectionsView &) const;
};
enum class ConnectionCommandKind { Voice, CancelActivity, Media };
struct ConnectionCommand {
  ConnectionCommandKind kind=ConnectionCommandKind::Voice;
  VoiceAction voice=VoiceAction::None;
  std::string identity_scope, activity_id;
  uint64_t expected_revision=0;
  std::string voice_token{};
  MediaAction media=MediaAction::Toggle;
  std::string media_token{};
};
enum class CommandState { Rejected, Accepted, Confirmed, Unknown };
struct CommandReceipt { CommandState state=CommandState::Rejected; std::string message; };
struct ConnectionPorts {
  void *context=nullptr;
  // One bounded read on the foreground owner, at most twice per second.
  // Clear unavailable/private data on rebinding; do not start a provider here.
  bool (*read)(void *,size_t offset,const char *selected,ConnectionsView &)=nullptr;
  // Revalidate current identity, capability and revision at the provider.
  CommandReceipt (*command)(void *,const ConnectionCommand &)=nullptr;
};
struct VoiceAvailabilityFacts {
  bool initialized=false,enabled=false,suspended=false,failed=false;
  bool listening=false,speaking=false,activation_ready=false,opening=false,thinking=false;
  bool microphone_enabled=false,network_address=false;
};
// App-owned presentation rules. Products choose providers and supply neutral
// facts; providers still revalidate identity and action when executing commands.
bool project_connection_voice(ConnectionsView &,const VoiceAvailabilityFacts &);
// Provider-admitted action plus presentation facts. The ticket is returned
// unchanged with a command; Pet never interprets service authority or sessions.
struct VoiceControlPresentation {
  VoiceAction action=VoiceAction::None;
  bool failed=false,listening=false,thinking=false,speaking=false;
  std::string token;
};
bool project_connection_voice(ConnectionsView &,const VoiceControlPresentation &);
template<class ProviderView> bool project_connection_activities(const ProviderView &view,ConnectionsView &out){
  out.activities=ConnectionState::Unavailable;out.revision=out.configuration_revision=0;
  out.total=out.offset=out.count=0;out.items={};out.selected={};
  if(out.identity_scope.empty()||view.identity_scope!=out.identity_scope||view.count>out.items.size())return false;
  out.activities=ConnectionState::Ready;out.revision=view.revision;out.configuration_revision=view.configuration_revision;
  out.total=view.total;out.offset=view.offset;out.count=view.count;
  for(size_t i=0;i<out.count;++i){const auto &item=view.items[i];out.items[i]={item.id,item.title,item.status,item.detail,item.cancellable};}
  const auto &selected=view.selected;out.selected={selected.id,selected.title,selected.status,selected.detail,selected.cancellable};
  return true;
}
} // namespace lunaredu::pet
