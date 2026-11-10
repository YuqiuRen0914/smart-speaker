#include "lunaros_files_browser.h"
#include "desktop/shell/shell.h"
#include "desktop/ui/content_layout.h"
#include <algorithm>
#include <cstring>
#include <cctype>

namespace lunaredu::desktop {
std::shared_ptr<const FileBrowserState> file_browser(const DesktopShell &shell) {
  return shell.surface_state && shell.surface_state->kind == &kFileSurfaceStateKind
      ? std::static_pointer_cast<const FileBrowserState>(shell.surface_state) : nullptr;
}
void publish_file_browser(DesktopShell &shell, std::shared_ptr<const FileBrowserState> state) {
  shell.surface_state = std::move(state);
}

namespace {
std::shared_ptr<FileBrowserState> edit(DesktopShell &s) {
  return file_browser(s)?std::make_shared<FileBrowserState>(*file_browser(s)):std::make_shared<FileBrowserState>();
}
void publish(DesktopShell &s,std::shared_ptr<FileBrowserState> value) { s.surface_requested=value->request!=FileAction::None;publish_file_browser(s,std::move(value)); }
void open_info(FileBrowserState &s) {
  if(s.history.size()>=8) return;
  s.history.push_back(s.location);s.location.view=FileView::Information;s.location.focus=s.location.scroll=0;
  s.request=FileAction::Close;
}
void activate(FileBrowserState &s) {
  if(s.location.view==FileView::Preview && s.location.focus==3) { open_info(s);return; }
  if(!s.content || !s.content->available || !s.content->message.empty()) { s.request=FileAction::Reload;return; }
  if(s.location.view==FileView::Sort) { s.request=s.location.focus?FileAction::SortSize:FileAction::SortName;return; }
  if(s.location.view==FileView::Preview) {
    if(s.content->file.kind==FileKind::Audio || s.content->file.kind==FileKind::Video) {
      if(s.location.focus==0) s.request=FileAction::Toggle;
      else if(s.location.focus==3) open_info(s);
    } else open_info(s);
    return;
  }
  if(s.location.view!=FileView::Directory) return;
  const size_t entries=s.content->entries.size();unsigned row=s.location.focus;
  if(row<entries) {
    if(s.history.size()>=8) return;
    const auto entry=s.content->entries[row];
    if(!file_user_path(entry.path)) return;
    s.history.push_back(s.location);s.location={};s.location.path=entry.path;s.wallpaper_applied=false;
    s.location.view=entry.kind==FileKind::Directory?FileView::Directory:entry.kind==FileKind::Binary?FileView::Information:FileView::Preview;
    auto content=std::make_shared<FileContent>();content->file=entry;s.content=content;s.request=FileAction::Open;
  } else {
    row-=unsigned(entries);
    if(s.location.page && row--==0) { s.request=FileAction::PageBack;return; }
    if(s.content->more && row--==0) { s.request=FileAction::PageForward;return; }
    if(s.history.size()<8) { s.history.push_back(s.location);s.location.view=FileView::Sort;s.location.focus=s.size_sort?1:0;s.location.scroll=0; }
  }
}
}
void open_desktop_files(DesktopShell &s) {
  auto state=std::make_shared<FileBrowserState>();state->loading=true;publish(s,state);
}
bool file_command(DesktopShell &shell,DesktopCommand cmd) {
  if(!file_browser(shell)) open_desktop_files(shell);
  auto s=edit(shell);
  if(cmd==DesktopCommand::Back) {
    if(s->history.empty()) return false;
    s->request=FileAction::Back;publish(shell,s);return true;
  }
  if(cmd==DesktopCommand::Confirm) {
    if(s->location.view==FileView::Preview&&s->content&&s->content->file.kind==FileKind::Jpeg&&s->content->image)return file_control(shell,FileControl::Wallpaper);
    activate(*s);publish(shell,s);return true;
  }
  if(s->loading) return true;
  const bool reverse=cmd==DesktopCommand::Previous || cmd==DesktopCommand::Up || cmd==DesktopCommand::Left;
  if(s->location.view==FileView::Preview && s->content &&
      (s->content->file.kind==FileKind::Audio || s->content->file.kind==FileKind::Video)) {
    if(cmd==DesktopCommand::Left || cmd==DesktopCommand::Right) {
      if(s->location.focus==1) { s->request=FileAction::Seek;s->value=std::clamp(s->playback.position+(reverse?-5:5),0.,s->playback.duration); }
      else if(s->location.focus==2 && s->capabilities.audio_output) { s->request=FileAction::Volume;s->value=std::clamp<int>(s->playback.volume+(reverse?-10:10),0,100); }
    } else {
      constexpr unsigned silent_controls[]={0,1,3};
      if(s->capabilities.audio_output) s->location.focus=(s->location.focus+4+(reverse?-1:1))%4;
      else {
        unsigned i=0;while(i<2 && silent_controls[i]!=s->location.focus)++i;
        s->location.focus=silent_controls[(i+3+(reverse?-1:1))%3];
      }
    }
  } else if(s->location.view==FileView::Preview || s->location.view==FileView::Information) {
    s->location.scroll=unsigned(std::clamp<int>(int(s->location.scroll)+(reverse?-32:32),0,8192));
  } else {
    const unsigned rows=file_rows(*s),visible=ContentLayout(shell.preferences.auto_hide_status).rows();
    s->location.focus=(s->location.focus+rows+(reverse?-1:1))%rows;
    if(s->location.focus<s->location.scroll) s->location.scroll=s->location.focus;
    if(s->location.focus>=s->location.scroll+visible) s->location.scroll=s->location.focus-visible+1;
  }
  publish(shell,s);return true;
}
bool file_scroll(DesktopShell &shell,unsigned value) {
  if(!file_browser(shell)||file_browser(shell)->loading)return false;
  auto s=edit(shell);
  if(s->location.view==FileView::Directory||s->location.view==FileView::Sort){
    s->location.scroll=std::min(value,file_rows(*s)-1);s->location.focus=s->location.scroll;
  }else s->location.scroll=std::min(value,8192u);
  publish(shell,s);return true;
}
bool file_control(DesktopShell &shell,FileControl control,unsigned index,double value) {
  if(!file_browser(shell))return false;
  auto s=edit(shell);
  if(control==FileControl::Information){if(s->location.view!=FileView::Preview||s->loading||!s->content)return false;open_info(*s);publish(shell,s);return true;}
  if(s->loading)return false;
  if(control==FileControl::Wallpaper){
    if(s->location.view!=FileView::Preview||!s->content||!s->content->available||s->content->file.kind!=FileKind::Jpeg||!s->content->image||
       !s->content->message.empty()||!file_user_path(s->location.path)||!valid_wallpaper_path(s->location.path))return false;
    auto preferences=shell.preferences;preferences.wallpaper=DesktopWallpaper::Image;
    std::strcpy(preferences.wallpaper_path,s->location.path.c_str());
    // Only the background changes; retain this preview's contact/navigation state.
    shell.preferences=preferences;shell.wallpaper_image=s->content->image;shell.preferences_changed=true;
    s->wallpaper_applied=true;publish(shell,s);return true;
  }
  if(control==FileControl::Retry)s->request=FileAction::Reload;
  else if(control==FileControl::Row){
    if(s->location.view!=FileView::Directory&&s->location.view!=FileView::Sort)return false;
    if(index>=file_rows(*s))return false;
    s->location.focus=index;activate(*s);
  }else {
    if(s->location.view!=FileView::Preview||!s->content||
       (s->content->file.kind!=FileKind::Audio&&s->content->file.kind!=FileKind::Video))return false;
    if(control==FileControl::Toggle){s->location.focus=0;s->request=FileAction::Toggle;}
    else if(control==FileControl::Seek){if(!s->capabilities.seek||!std::isfinite(value))return false;s->location.focus=1;s->request=FileAction::Seek;s->value=std::clamp(value,0.,1.)*s->playback.duration;}
    else if(control==FileControl::Volume){if(!s->capabilities.audio_output||!s->capabilities.volume||!std::isfinite(value))return false;s->location.focus=2;s->request=FileAction::Volume;s->value=std::clamp(value,0.,100.);}
  }
  publish(shell,s);return true;
}
} // namespace lunaredu::desktop
