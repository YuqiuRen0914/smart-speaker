#include "desktop/ui/status_bar.h"
namespace lunaredu::desktop {
bool status_overlay_shows_bar(const DesktopStatus &status,const StatusNavigation &nav,bool automatic){
  return nav.showing(automatic)||status.peer_severity==DesktopStatus::Severity::Critical||status.peer_severity==DesktopStatus::Severity::Conflict;
}
}
