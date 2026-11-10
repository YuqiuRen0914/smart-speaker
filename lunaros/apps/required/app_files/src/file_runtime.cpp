#include "lunaros_files_runtime.h"
#include <algorithm>
#include <cstring>
#include <cctype>

namespace lunaredu::desktop {
namespace media = lunaredu::lunaros::media;
namespace {
int compare(const FileEntry &a,const FileEntry &b,bool size) {
  if((a.kind==FileKind::Directory)!=(b.kind==FileKind::Directory)) return a.kind==FileKind::Directory?-1:1;
  if(size && a.size!=b.size) return a.size>b.size?-1:1;
  return a.path.compare(b.path);
}
bool child(const std::string &path,const std::string &parent) {
  if(!file_user_path(path)) return false;
  return parent=="/"?(path=="/scripts" || path=="/performances" || path=="/saves"):path.substr(0,path.find_last_of('/'))==parent;
}
bool text_valid(std::string &text,bool truncated) {
  size_t i=0;
  if(text.compare(0,3,"\xef\xbb\xbf")==0) text.erase(0,3);
  while(i<text.size()) {
    const unsigned char first=text[i];
    unsigned length=first<0x80?1:first>=0xc2&&first<=0xdf?2:first>=0xe0&&first<=0xef?3:first>=0xf0&&first<=0xf4?4:0;
    if(!length || !first || (first<32 && first!='\n' && first!='\r' && first!='\t')) return false;
    if(i+length>text.size()) { if(truncated) { text.resize(i);return true; }return false; }
    for(unsigned n=1;n<length;++n) if((static_cast<unsigned char>(text[i+n])&0xc0)!=0x80) return false;
    const unsigned char second=length>1?text[i+1]:0;
    if((length==3 && ((first==0xe0 && second<0xa0)||(first==0xed && second>=0xa0))) ||
       (length==4 && ((first==0xf0 && second<0x90)||(first==0xf4 && second>=0x90)))) return false;
    i+=length;
  }
  return true;
}
}
struct FileRuntime::Impl {
  FileProvider provider;
  media::FileBackend backend;
  media::Playback player;
  bool media_owned=false;
  explicit Impl(FileProvider p,media::FileAudioOutput *audio):provider(p),backend(p.io,audio),player(&backend) {}
  bool stop() {
    const auto token=player.snapshot().token;
    const bool stopped=!token||player.stop(token)==media::Result::ok;
    media_owned=!stopped;return stopped;
  }
  void error(FileBrowserState &s,const char *message) {
    auto content=std::make_shared<FileContent>(s.content?*s.content:FileContent{});
    content->message=message;s.content=std::move(content);s.loading=false;
  }
  void list(FileBrowserState &s) {
    auto content=std::make_shared<FileContent>();
    struct Page {
      FileBrowserState *state;FileContent *content;size_t count=0;bool overflow=false;
    } page{&s,content.get()};
    const bool ok=provider.list && provider.list(provider.io.context,s.location.path.c_str(),[](const FileEntry &entry,void *opaque) {
      auto &p=*static_cast<Page *>(opaque);auto &s=*p.state;
      if(++p.count>4096) { p.overflow=true;return false; }
      if(!child(entry.path,s.location.path)) return true;
      if(!s.location.anchor.path.empty()) {
        const int c=compare(entry,s.location.anchor,s.size_sort);
        if(s.location.before_anchor?c>=0:c<=0) return true;
      }
      auto &rows=p.content->entries;
      if(rows.size()==8) p.content->more=true;
      auto at=std::lower_bound(rows.begin(),rows.end(),entry,[&](const auto &a,const auto &b) {
        const int c=compare(a,b,s.size_sort);return s.location.before_anchor?c>0:c<0;
      });
      // Never grow to a ninth entry; replace the current boundary in place.
      if(rows.size()<8) rows.insert(at,entry);
      else if(at!=rows.end()) { const auto index=at-rows.begin();rows.pop_back();rows.insert(rows.begin()+index,entry); }
      return true;
    },&page);
    if(s.location.before_anchor) { std::reverse(content->entries.begin(),content->entries.end());content->more=true; }
    if(!ok || page.overflow) content->message=page.overflow?"目录项目过多":"读取失败，请重试";
    s.content=std::move(content);s.loading=false;
    s.location.focus=std::min(s.location.focus,file_rows(s)-1);
    s.location.scroll=std::min(s.location.scroll,s.location.focus);
  }
  void preview(FileBrowserState &s) {
    auto content=std::make_shared<FileContent>(s.content?*s.content:FileContent{});
    content->message.clear();content->image.reset();content->text.clear();
    const auto &file=content->file;
    if(!file_user_path(s.location.path) || s.location.path!=file.path) { s.content=content;error(s,"文件位置无效");return; }
    if(s.location.view==FileView::Information) { s.content=content;return; }
    if(file.kind==FileKind::Text) {
      const size_t count=std::min<size_t>(4096,file.size);content->text.resize(count);content->text_truncated=count<file.size;
      if(count && (!provider.io.read || !provider.io.read(provider.io.context,file.path.c_str(),0,reinterpret_cast<uint8_t *>(content->text.data()),count))) content->message="读取失败，请重试";
      else if(!text_valid(content->text,content->text_truncated)) content->message="仅支持 UTF-8 文本";
    } else if(file.kind==FileKind::Jpeg) {
      if(!provider.io.jpeg) content->message="图片解码未启用";
      else if(!file.size || file.size>128*1024) content->message="图片文件过大或为空";
      else {
        std::vector<uint8_t> jpeg(file.size);auto image=std::make_shared<FilePixels>();
        if(!provider.io.read || !provider.io.read(provider.io.context,file.path.c_str(),0,jpeg.data(),jpeg.size()) || !provider.io.jpeg(provider.io.context,jpeg.data(),jpeg.size(),*image) ||
            !image->width || !image->height || image->width>320 || image->height>240 || image->pixels.size()!=size_t(image->width)*image->height) content->message="图片读取或解码失败";
        else content->image=std::move(image);
      }
    } else if(file.kind==FileKind::Audio || file.kind==FileKind::Video) {
      const auto dot=file.path.find_last_of('.');
      const auto suffix=dot==std::string::npos?std::string{}:file.path.substr(dot);
      media::Format format=media::Format::count;
      std::string extension=suffix;std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
      if(file.kind==FileKind::Audio) {
        if(!s.capabilities.audio_output) content->message="当前设备没有声音输出";
        else if(extension==".wav") format=media::Format::wav_pcm16;
      } else if(extension==".mjpeg" || extension==".mjpg") format=media::Format::mjpeg;
      if(content->message.empty()) {
        if(format==media::Format::count) content->message="当前设备不支持此编码";
        else {
          backend.source_size(file.size);
          const media::Request request{file.path.c_str(),format,uint16_t(format==media::Format::mjpeg?240:0),uint16_t(format==media::Format::mjpeg?240:0),format==media::Format::wav_pcm16};
          const auto result=player.open(request);
          media_owned=bool(player.snapshot().token);
          if(result!=media::Result::ok) { content->message="无法打开此媒体文件";stop(); }
          else { s.loading=backend.loading();s.playback=player.snapshot();s.silent=!s.capabilities.audio_output || format==media::Format::mjpeg; }
        }
      }
    } else content->message="此格式暂不支持预览";
    s.content=std::move(content);
  }
};
FileRuntime::FileRuntime(FileProvider p,media::FileAudioOutput *audio):impl_(new Impl(p,audio)) {}
FileRuntime::~FileRuntime()=default;
bool FileRuntime::cleanup_pending() const{return impl_->media_owned;}
bool FileRuntime::retire(){return impl_->stop();}
std::shared_ptr<const FilePixels> FileRuntime::load_wallpaper(const char *path) {
  auto &r=*impl_;
  if(!path||!file_user_path(path)||file_kind(path,false)!=FileKind::Jpeg||!r.provider.ready||!r.provider.ready(r.provider.io.context)||!r.provider.list)return {};
  struct Match {std::string path;FileEntry file;size_t count=0;bool overflow=false;} match;
  match.path=path;
  const auto parent=match.path.substr(0,match.path.find_last_of('/'));
  const bool listed=r.provider.list(r.provider.io.context,parent.c_str(),[](const FileEntry &entry,void *p){
    auto &m=*static_cast<Match *>(p);if(++m.count>4096){m.overflow=true;return false;}
    if(entry.path==m.path)m.file=entry;
    return true;
  },&match);
  if(!listed||match.overflow||match.file.path.empty()||match.file.kind!=FileKind::Jpeg)return {};
  FileBrowserState state;state.location.path=path;state.location.view=FileView::Preview;
  auto content=std::make_shared<FileContent>();content->file=std::move(match.file);state.content=std::move(content);
  r.preview(state);return state.content?state.content->image:nullptr;
}
bool FileRuntime::update(std::shared_ptr<const FileBrowserState> &published,uint32_t now,bool active) {
  auto &r=*impl_;
  if(!active) {
    if(!r.media_owned)return false;
    const bool owned=r.media_owned;
    const bool stopped=r.stop();
    if(owned && published) {
      auto s=std::make_shared<FileBrowserState>(*published);s->playback=r.player.snapshot();s->loading=false;
      if(!stopped) r.error(*s,"播放器停止失败，正在重试");
      published=std::move(s);
    }
    return owned;
  }
  if(!published) published=std::make_shared<FileBrowserState>();
  const FileAction request=published->request;
  const bool ready=r.provider.ready && r.provider.ready(r.provider.io.context);
  if(!ready) {
    const bool old=published->content && !published->content->available;
    const bool stopped=r.stop();
    if(old && stopped) return false;
    auto s=std::make_shared<FileBrowserState>(*published);auto c=std::make_shared<FileContent>();c->available=false;c->file=s->content?s->content->file:FileEntry{};
    c->message=stopped?"存储已离线":"播放器停止失败，正在重试";s->content=std::move(c);s->loading=false;s->playback=r.player.snapshot();published=std::move(s);return true;
  }
  const bool restored=published->content && !published->content->available;
  if(request==FileAction::None && !restored && !r.backend.tick(now)) return false;
  auto s=std::make_shared<FileBrowserState>(*published);s->capabilities=r.backend.capabilities();s->request=FileAction::None;
  FileAction action=restored?FileAction::Reload:request;
  const bool change=action==FileAction::Reload || action==FileAction::Open || action==FileAction::Back || action==FileAction::Close ||
      action==FileAction::PageForward || action==FileAction::PageBack || action==FileAction::SortName || action==FileAction::SortSize;
  if(change && !r.stop()) { r.error(*s,"播放器停止失败，请重试");s->request=FileAction::Close;published=std::move(s);return true; }
  if(action==FileAction::Back && !s->history.empty()) { s->location=s->history.back();s->history.pop_back();action=FileAction::Reload; }
  if(action==FileAction::SortName || action==FileAction::SortSize) {
    s->size_sort=action==FileAction::SortSize;
    if(!s->history.empty()) { s->location=s->history.back();s->history.pop_back(); }
    s->location.anchor={};s->location.page=s->location.focus=s->location.scroll=0;s->location.before_anchor=false;action=FileAction::Reload;
  }
  if((action==FileAction::PageForward || action==FileAction::PageBack) && s->content && !s->content->entries.empty()) {
    const bool back=action==FileAction::PageBack;
    s->location.anchor=back?s->content->entries.front():s->content->entries.back();s->location.before_anchor=back;
    s->location.page=back?(s->location.page?s->location.page-1:0):s->location.page+1;
    s->location.focus=s->location.scroll=0;action=FileAction::Reload;
  }
  if(action==FileAction::Open || action==FileAction::Reload) {
    s->loading=false;
    if(s->location.view==FileView::Directory) r.list(*s);
    else if(s->location.view!=FileView::Sort) r.preview(*s);
  } else if(action==FileAction::Toggle && !s->loading && r.player.snapshot().token) {
    const auto snapshot=r.player.snapshot();const auto result=snapshot.state==media::State::playing?r.player.pause(snapshot.token):r.player.play(snapshot.token);
    if(result!=media::Result::ok) r.error(*s,"播放失败，请返回后重试");
  } else if(action==FileAction::Seek && !s->loading && r.player.snapshot().token) {
    if(r.player.seek(r.player.snapshot().token,s->value)!=media::Result::ok) r.error(*s,"无法跳转到此位置");
  } else if(action==FileAction::Volume && r.player.snapshot().token) {
    if(r.player.volume(r.player.snapshot().token,int(s->value))!=media::Result::ok) r.error(*s,"无法调整音量");
  }
  const auto token=r.player.snapshot().token;
  if(token) {
    const auto status=r.backend.status();
    if(status.token==token) r.player.report(token,status.state,status.position,status.duration);
    s->playback=r.player.snapshot();s->loading=r.backend.loading();
    if(status.state==media::State::error) r.error(*s,"媒体读取或解码失败");
    if(s->content && s->content->image!=r.backend.image()) { auto c=std::make_shared<FileContent>(*s->content);c->image=r.backend.image();s->content=std::move(c); }
  } else { s->playback={};s->loading=false; }
  published=std::move(s);return true;
}
} // namespace lunaredu::desktop

namespace lunaredu::files {
RuntimeSession::RuntimeSession(desktop::FileProvider provider,lunaros::media::FileAudioOutput *output):runtime_(provider,output){}
bool RuntimeSession::enter(std::string &error) {
  if(!state_)state_=std::make_shared<desktop::FileBrowserState>();
  error.clear();return true;
}
bool RuntimeSession::update(uint32_t now,bool active){now_=now;return runtime_.update(state_,now,active);}
const std::shared_ptr<const desktop::FileBrowserState> &RuntimeSession::snapshot()const{return state_;}
void RuntimeSession::publish(std::shared_ptr<const desktop::FileBrowserState> state){state_=std::move(state);}
bool RuntimeSession::retire(std::string &error) {
  (void)runtime_.update(state_,now_,false);
  if(!runtime_.retire()||runtime_.cleanup_pending()){error="file_player_stop_pending";return false;}
  error.clear();return true;
}
void RuntimeSession::rollback(){state_.reset();}
} // namespace lunaredu::files
