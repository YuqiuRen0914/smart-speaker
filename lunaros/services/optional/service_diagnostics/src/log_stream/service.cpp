#include "lunaros_diagnostics_log_stream.h"
#include "log_stream_transport.h"
#include "lifecycle.h"
namespace lunaredu::lunaros {
namespace { DiagnosticsLogStream *owner=nullptr; }
DiagnosticsLogStream::~DiagnosticsLogStream() {
  std::string ignored;shutdown(ignored);
}
bool DiagnosticsLogStream::shutdown(std::string &error) {
  if(owner==this){stop_diagnostics_log_stream();owner=nullptr;}
  error.clear();return true;
}
bool DiagnosticsLogStream::capture_boot(std::string &error) {
  error.clear();
  if(owner&&owner!=this){error="log_stream_already_owned";return false;}
  if(!config_.port){error="log_stream_invalid_port";return false;}
  if(captured_)return true;
  log_stream::initialize({config_.port,config_.greeting});
  owner=this;captured_=true;return true;
}
bool diagnostics_log_stream_selected(){return owner!=nullptr;}
bool start_diagnostics_log_stream(std::string &error) {
  if(!owner){error.clear();return true;}
  if(!owner->capture_boot(error))return false;
  owner->started_=true;return true;
}
void stop_diagnostics_log_stream() {
  if(!owner)return;
  owner->started_=false;
  if(owner->captured_)log_stream::shutdown();
  owner->captured_=false;
}
void DiagnosticsLogStream::tick(){if(started_&&owner==this)log_stream::tick();}
bool DiagnosticsLogStream::client_connected()const {
  return started_&&owner==this&&log_stream::client_connected();
}
}
