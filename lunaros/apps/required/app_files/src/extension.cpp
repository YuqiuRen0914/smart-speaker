#include "lunaros_files_extension.h"
#include "surface_binding.h"
#include "desktop/shell/shell.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#include "graphics/scene.h"
namespace lunaredu::lunaros::graphics {
void build_file_surface(Scene &,const desktop::DesktopShell &);
bool apply_file_action(desktop::DesktopShell &,Action);
}
namespace lunaredu::desktop {
namespace {
uint64_t revision(const DesktopShell &s){
 uint64_t hash=1469598103934665603ULL;const auto add=[&](uint64_t n){hash^=n;hash*=1099511628211ULL;};
  if(file_browser(s)){const auto &f=*file_browser(s);add(unsigned(f.location.view));add(f.location.page);add(f.loading);for(unsigned char c:f.location.path)add(c);if(f.content){add(f.content->available);add(!f.content->message.empty());add(unsigned(f.content->file.kind));if(f.location.view==desktop::FileView::Directory||f.location.view==desktop::FileView::Sort)add(reinterpret_cast<uintptr_t>(f.content.get()));}else add(0);}
 return hash;
}
}
namespace {
struct FileSurfaceBinding { std::weak_ptr<files::Workspace> workspace; };
const DesktopSurfaceExtension extension=[] {
 DesktopSurfaceExtension value;
 value.foreground_id="os.files";
 value.row_activation=DesktopRowActivation::Disabled;
 value.application=&files::desktop_application();
 value.title=[](const DesktopShell &) { return "文件"; };
 value.poll=[](DesktopShell &shell,uint32_t now) { return update_desktop_files(shell,now); };
 value.open=open_desktop_files;
 value.command=file_command;
 value.revision=revision;
 value.build=[](lunaros::graphics::Scene &scene,const DesktopShell &s,const DesktopStatus &) {lunaros::graphics::build_file_surface(scene,s);};
 value.action=lunaros::graphics::apply_file_action;
 return value;
}();
}
const DesktopSurfaceExtension &file_surface_extension(){ return extension; }
std::shared_ptr<files::Workspace> file_workspace(DesktopShell &shell){
 const auto context=desktop_surface_context(shell,DesktopSurface::Files,extension);
 return context?std::static_pointer_cast<const FileSurfaceBinding>(context)->workspace.lock():nullptr;
}
bool install_file_surface(DesktopShell &shell,const std::shared_ptr<files::Workspace> &workspace){
 if(!workspace)return false;
 const auto context=desktop_surface_context(shell,DesktopSurface::Files,extension);
 if(context){
   if(auto owner=std::static_pointer_cast<const FileSurfaceBinding>(context)->workspace.lock())return owner==workspace;
   if(!remove_desktop_surface(shell,DesktopSurface::Files,extension,context))return false;
 }
 return install_desktop_surface(shell,DesktopSurface::Files,extension,
     std::make_shared<const FileSurfaceBinding>(FileSurfaceBinding{workspace}));
}
bool remove_file_surface(DesktopShell &shell,const std::shared_ptr<files::Workspace> &workspace){
 if(!workspace||file_workspace(shell)!=workspace)return false;
 if(const auto browser=file_browser(shell);browser&&browser->playback.token)return false;
 return remove_desktop_surface(shell,DesktopSurface::Files,extension,
     desktop_surface_context(shell,DesktopSurface::Files,extension));
}
}
