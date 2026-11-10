#pragma once
#include "desktop/shell/shell.h"
namespace lunaredu::desktop {
enum class ConsoleReport { Status, DisplayStatus, TouchStatus, Orientation, Bars, Redraw };
// Standard UI console routing. Optional App commands are dispatched through
// declared callbacks; the generic OS never imports their implementation.
class ConsolePort {
 public:
  virtual ~ConsolePort() = default;
  virtual void report(ConsoleReport) = 0;
  virtual bool desktop(DesktopCommand) = 0;
  virtual bool application_command(const char *) { return false; }
};
bool serial_command(ConsolePort &port,const char *line);
}
