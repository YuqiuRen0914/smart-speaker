#include "desktop/shell/shell.h"
#include "lunaros_files_extension.h"
#include "surface_binding.h"
#include "lunaros_files_runtime.h"
#include "lunaros_storage.h"
#if defined(ESP_PLATFORM)
#include <sdkconfig.h>
#endif
#if CONFIG_LUNAROS_DESKTOP_FILE_JPEG
#include "image_jpeg_decoder.h"
#endif
#include <cstring>
namespace lunaredu::desktop {
namespace {
bool ready(void *) { return lunaros::storage_ready(); }
bool read(void *,const char *path,size_t offset,uint8_t *bytes,size_t size) {
  return lunaros::storage_user_file_path_allowed(path) && lunaros::storage_read_range(path,offset,bytes,size);
}
bool list(void *,const char *path,FileVisitor visit,void *context) {
  if(!std::strcmp(path,"/")) {
    for(const char *root:{"/scripts","/performances","/saves"}) {
      if(lunaros::storage_path_exists(root) && !visit({root,0,0,FileKind::Directory},context)) break;
    }
    return true;
  }
  if(!lunaros::storage_user_file_path_allowed(path)) return false;
  struct Visitor { FileVisitor visit;void *context; } visitor{visit,context};
  return lunaros::storage_list_entries(path,[](const lunaros::StorageListEntry &entry,void *opaque) {
    const auto &v=*static_cast<Visitor *>(opaque);
    if(!lunaros::storage_user_file_path_allowed(entry.path.c_str())) return true;
    return v.visit({entry.path,entry.size_bytes,entry.modified_ms,file_kind(entry.path,entry.is_directory)},v.context);
  },&visitor);
}
#if CONFIG_LUNAROS_DESKTOP_FILE_JPEG
bool jpeg(void *,const uint8_t *data,size_t size,FilePixels &output) {
  Rgb565ImageBuffer image;std::string error;
  if(!decode_jpeg_to_rgb565(data,size,image,error)) return false;
  output.width=image.width;output.height=image.height;
  output.pixels.resize(size_t(image.width)*image.height);
  for(size_t i=0;i<output.pixels.size();++i) output.pixels[i]=uint16_t(image.data.data()[2*i]) | uint16_t(image.data.data()[2*i+1])<<8;
  return true;
}
#endif
FileProvider provider() {
  FileProvider p{{nullptr,read,nullptr},ready,list};
#if CONFIG_LUNAROS_DESKTOP_FILE_JPEG
  p.io.jpeg=jpeg;
#endif
  return p;
}
}
} // namespace lunaredu::desktop
namespace lunaredu::files {
using namespace desktop;
Workspace::Workspace() = default;
Workspace::~Workspace() = default;
bool Workspace::cleanup_pending() const { return file_runtime_&&file_runtime_->cleanup_pending(); }
bool Workspace::retire(){return !file_runtime_||file_runtime_->retire();}
bool Workspace::update(DesktopShell &shell,uint32_t now,bool visible) {
  // One App workspace, serialized by its foreground input owner.
  // The observer may be selected without a Files page ever being opened.
  // Own no runtime or storage/player polling unless a surface, wallpaper or
  // unacknowledged media retirement actually requires it.
  if(shell.surface!=DesktopSurface::Files&&!file_browser(shell)&&
     shell.preferences.wallpaper!=DesktopWallpaper::Image&&
     (!file_runtime_||!file_runtime_->cleanup_pending())){
    file_runtime_.reset();wallpaper_attempt_.clear();wallpaper_storage_ready_=false;return false;
  }
  if(!file_runtime_) file_runtime_.reset(new FileRuntime(provider(),audio_output_));
  bool wallpaper_changed=false;
  const bool storage_ready=ready(nullptr);
  if(!storage_ready){wallpaper_attempt_.clear();wallpaper_storage_ready_=false;}
  if(shell.preferences.wallpaper==DesktopWallpaper::Image&&storage_ready&&!shell.wallpaper_image&&
     (!wallpaper_storage_ready_||wallpaper_attempt_!=shell.preferences.wallpaper_path)){
    wallpaper_attempt_=shell.preferences.wallpaper_path;wallpaper_storage_ready_=true;
    shell.wallpaper_image=file_runtime_->load_wallpaper(wallpaper_attempt_.c_str());
    wallpaper_changed=bool(shell.wallpaper_image);
  }
  if(shell.preferences.wallpaper!=DesktopWallpaper::Image)wallpaper_attempt_.clear();
  const bool active=visible && shell.surface==DesktopSurface::Files;
  auto state = file_browser(shell);
  const bool owns_surface=bool(state)||active;
  bool changed=file_runtime_->update(state,now,active)||wallpaper_changed;
  // An inactive observer must never replace another App's surface snapshot.
  if(owns_surface)publish_file_browser(shell,std::move(state));
  if(!active && !shell.home_navigation.exiting() && file_browser(shell) && !file_browser(shell)->playback.token) {
    shell.surface_state.reset();changed=true;
  }
  if(owns_surface)shell.surface_requested=file_browser(shell) && file_browser(shell)->request!=FileAction::None;
  return changed;
}
bool Workspace::configure_audio_output(lunaredu::lunaros::media::FileAudioOutput *output) {
  if(file_runtime_)return false;
  audio_output_=output;return true;
}
}
namespace lunaredu::desktop {
bool update_desktop_files(DesktopShell &shell,uint32_t now,bool visible) {
  const auto workspace=file_workspace(shell);
  return workspace&&workspace->update(shell,now,visible);
}
bool configure_desktop_file_audio_output(DesktopShell &shell,lunaros::media::FileAudioOutput *output) {
  const auto workspace=file_workspace(shell);return workspace&&workspace->configure_audio_output(output);
}
void refresh_desktop_files(DesktopShell &shell) { (void)update_desktop_files(shell,shell.now_ms); }
}
