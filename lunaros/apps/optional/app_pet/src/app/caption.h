#pragma once
#include "lunaredu_pet_ui.h"
namespace lunaredu::pet {
inline const std::string &home_caption(const UiSnapshot &view){
  if(!view.connections.voice.caption.empty())return view.connections.voice.caption;
  if(!view.setup_caption.empty())return view.setup_caption;
  if(!view.message.empty())return view.message;
  if(view.connections.media.state==ConnectionState::Ready&&!view.connections.media.caption.empty())return view.connections.media.caption;
  return view.connections.voice.status;
}
}
