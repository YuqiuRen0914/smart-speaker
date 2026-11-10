#pragma once
#include "lunaros_files_model.h"
#include "desktop/ui/surface_extension.h"
#include "desktop/ui/wallpaper.h"
#include <memory>
#include <string>
#include <vector>

namespace lunaredu::desktop {
struct DesktopShell;
enum class DesktopCommand : uint8_t;
// Snapshot publication is immutable. Render mailbox copies retain the content
// and decoded frame until their draw finishes, without putting pixels on stack.
std::shared_ptr<const FileBrowserState> file_browser(const DesktopShell &);
void publish_file_browser(DesktopShell &, std::shared_ptr<const FileBrowserState>);
void open_desktop_files(DesktopShell &shell);
bool file_command(DesktopShell &shell, DesktopCommand command);
bool file_control(DesktopShell &,FileControl,unsigned index=0,double value=0);
bool file_scroll(DesktopShell &,unsigned value);
bool update_desktop_files(DesktopShell &, uint32_t now_ms, bool visible = true);
} // namespace lunaredu::desktop
