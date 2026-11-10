#pragma once
#include "lunaros_files_model.h"
#include "app/lifecycle/application_surface_session.h"
#include "lunaros_files_backend.h"
#include <memory>
#include <utility>

namespace lunaredu::desktop {
using FileVisitor = bool (*)(const FileEntry &, void *);
struct FileProvider {
  lunaredu::lunaros::media::FileIo io;
  bool (*ready)(void *) = nullptr;
  bool (*list)(void *, const char *, FileVisitor, void *) = nullptr;
};
// Owned by the input/service task, never copied into a rendering job. All calls
// are serialized by that owner. No task or timer is created by this helper.
// Before destruction, the owner must update(active=false) until the published
// playback token is zero; stop failures keep the output lease for retry.
class FileRuntime {
public:
  explicit FileRuntime(FileProvider, lunaredu::lunaros::media::FileAudioOutput * = nullptr);
  ~FileRuntime();
  FileRuntime(const FileRuntime &) = delete;
  FileRuntime &operator=(const FileRuntime &) = delete;
  bool update(std::shared_ptr<const FileBrowserState> &, uint32_t now_ms, bool active);
  std::shared_ptr<const FilePixels> load_wallpaper(const char *path);
  bool cleanup_pending() const;
  bool retire();
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace lunaredu::desktop

namespace lunaredu::files {
// A real Files session for hosts without DesktopShell. Storage, media and
// failure/retirement use FileRuntime, exactly as the desktop adapter does.
class RuntimeSession final:public app::ApplicationSurfaceSession {
  desktop::FileRuntime runtime_;
  std::shared_ptr<const desktop::FileBrowserState> state_;
  uint32_t now_=0;
public:
  explicit RuntimeSession(desktop::FileProvider provider,lunaros::media::FileAudioOutput *output=nullptr);
  bool enter(std::string &error)override;
  bool update(uint32_t now,bool active=true);
  const std::shared_ptr<const desktop::FileBrowserState> &snapshot()const;
  void publish(std::shared_ptr<const desktop::FileBrowserState> state);
  bool retire(std::string &error)override;
  void rollback()override;
};
}
