#include "lunaros_files_playback.h"

namespace lunaredu::lunaros::media {
Result probe(const Capabilities &c, const Request &r) {
  if (!r.path || r.path[0]!='/' || !r.path[1]) return Result::invalid;
  const auto bit=format_bit(r.format);
  if (!bit || !(c.supported_formats & bit)) return Result::unsupported_format;
  const auto kind=formats[static_cast<uint8_t>(r.format)].kind;
  if (kind==Kind::audio) {
    if (!c.audio) return Result::unavailable;
    if (!c.audio_output) return Result::no_audio_output;
  } else {
    if (!c.video) return Result::unavailable;
    if (!r.width || !r.height || r.width>c.max_video_width || r.height>c.max_video_height) return Result::resolution;
  }
  return Result::ok;
}
}
