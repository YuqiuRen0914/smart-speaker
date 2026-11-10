#pragma once
#include "lunaros_files_extension.h"
namespace lunaredu::desktop {
// Only the input owner acquires mutable state; renderers use FileBrowserState.
std::shared_ptr<files::Workspace> file_workspace(DesktopShell &);
}
