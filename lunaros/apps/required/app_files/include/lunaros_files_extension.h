#pragma once
#include "lunaros_files_browser.h"
#include "lunaros_files_backend.h"
#include "desktop/shell/shell.h"
#include <string>
namespace lunaredu::desktop { class FileRuntime; }
namespace lunaredu::files {
class Workspace {
public:
  Workspace();
  ~Workspace();
  Workspace(const Workspace &) = delete;
  Workspace &operator=(const Workspace &) = delete;
  bool update(desktop::DesktopShell &,uint32_t,bool);
  bool configure_audio_output(lunaros::media::FileAudioOutput *);
  bool cleanup_pending() const;
  bool retire(); // Physical player retirement independent of DesktopShell.
private:
  lunaros::media::FileAudioOutput *audio_output_=nullptr;
  std::unique_ptr<desktop::FileRuntime> file_runtime_;
  std::string wallpaper_attempt_;
  bool wallpaper_storage_ready_=false;
};
}
namespace lunaredu::desktop {
const DesktopSurfaceExtension &file_surface_extension();
bool install_file_surface(DesktopShell &,const std::shared_ptr<files::Workspace> &);
bool remove_file_surface(DesktopShell &,const std::shared_ptr<files::Workspace> &);
void refresh_desktop_files(DesktopShell &);
bool configure_desktop_file_audio_output(DesktopShell &,lunaredu::lunaros::media::FileAudioOutput *);
}

namespace lunaredu::files {
inline const desktop::DesktopApplication &desktop_application() {
  static const desktop::DesktopApplication application={"os.files","文件",desktop::DesktopIcon::Files,0x249f,desktop::DesktopAppKind::Surface,{},nullptr,desktop::DesktopAppCategory::System};
  return application;
}
}
