#pragma once
#include <array>
#include <algorithm>
#include <cstddef>

namespace lunaredu::lunaros::internal {
// Owned by httpd: handlers and its close callback run on the same executor.
// Reserve at least three session slots for commands, admission and WebSockets.
// Transport reuse never retains authentication or a response workspace.
class QueryConnections {
 public:
  void configure(size_t sessions) {
    sockets_.fill(-1);capacity_=sessions>3?std::min(sockets_.size(),sessions-3):0;
  }
  bool retain(int fd,bool eligible) {
    if(fd<0)return false;
    if(!eligible){closed(fd);return false;}
    for(size_t i=0;i<capacity_;++i)if(sockets_[i]==fd)return true;
    for(size_t i=0;i<capacity_;++i)if(sockets_[i]<0){sockets_[i]=fd;return true;}
    return false;
  }
  void closed(int fd) {for(auto &socket:sockets_)if(socket==fd)socket=-1;}
 private:
  std::array<int,2> sockets_{{-1,-1}};
  size_t capacity_=0;
};
inline QueryConnections &query_connections(){static QueryConnections value;return value;}
}
