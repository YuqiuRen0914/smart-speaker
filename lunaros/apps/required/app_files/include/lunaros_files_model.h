#pragma once
#include "lunaros_files_playback.h"
#include "app/lifecycle/application_surface_state.h"
#include "model/rgb565_image.h"
#include <memory>
#include <string>
#include <vector>
namespace lunaredu::desktop {
enum class FileKind : uint8_t { Directory, Text, Jpeg, Audio, Video, Binary };
enum class FileView : uint8_t { Directory, Preview, Information, Sort };
enum class FileAction : uint8_t { None, Reload, Open, Back, PageForward, PageBack, SortName, SortSize, Toggle, Seek, Volume, Close };
enum class FileControl : uint8_t { Row, Information, Seek, Volume, Toggle, Retry, Wallpaper };
struct FileEntry {
  std::string path;
  size_t size = 0;
  uint64_t modified_ms = 0;
  FileKind kind = FileKind::Binary;
};
struct FileLocation {
  std::string path = "/";
  FileView view = FileView::Directory;
  unsigned focus = 0, scroll = 0, page = 0;
  FileEntry anchor;
  bool before_anchor = false;
};
using FilePixels=lunaros::Rgb565Image;
struct FileContent {
  std::vector<FileEntry> entries; // <= 8, never the entire directory.
  FileEntry file;
  std::string text, message;
  std::shared_ptr<const FilePixels> image;
  bool available = true, more = false, text_truncated = false;
};
inline constexpr char kFileSurfaceStateKind = 0;
struct FileBrowserState : app::ApplicationSurfaceState {
  FileBrowserState() : app::ApplicationSurfaceState(&kFileSurfaceStateKind) {}
  FileLocation location;
  std::vector<FileLocation> history; // <= 8 locations, not page contents.
  std::shared_ptr<const FileContent> content;
  FileAction request = FileAction::Reload;
  double value = 0;
  bool size_sort = false, loading = false, silent = true;
  bool wallpaper_applied = false;
  lunaredu::lunaros::media::Snapshot playback;
  lunaredu::lunaros::media::Capabilities capabilities;
};
bool file_user_path(const std::string &path);
FileKind file_kind(const std::string &path, bool directory);
const char *file_name(const std::string &path);
unsigned file_rows(const FileBrowserState &state);
}
