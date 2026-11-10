#include "desktop/shell/console_commands.h"
#include <cstdio>
#include <cstring>
#include <iterator>

namespace lunaredu::desktop {
bool serial_command(ConsolePort &port,const char *line) {
  if(!line)return false;
  if(port.application_command(line))return true;
  constexpr const char *reports[]={"display status","touch status","display test","display bars","display ui"};
  constexpr ConsoleReport actions[]={ConsoleReport::DisplayStatus,ConsoleReport::TouchStatus,
      ConsoleReport::Orientation,ConsoleReport::Bars,ConsoleReport::Redraw};
  for(size_t i=0;i<std::size(reports);++i)if(!std::strcmp(line,reports[i])){port.report(actions[i]);return true;}
  if(std::strncmp(line,"desktop ",8))return false;
  constexpr const char *names[]={"previous","next","left","right","up","down","confirm","back","system","home"};
  for(size_t i=0;i<std::size(names);++i)if(!std::strcmp(line+8,names[i])) {
    const bool queued=port.desktop(static_cast<DesktopCommand>(i));
    std::printf("desktop command %s %s\n",names[i],queued?"queued":"busy");return true;
  }
  std::printf("unknown desktop command\n");return true;
}
} // namespace lunaredu::desktop
