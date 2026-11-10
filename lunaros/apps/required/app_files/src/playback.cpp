#include "lunaros_files_playback.h"

namespace lunaredu::lunaros::media {
Result Playback::open(const Request &request) {
    if (!backend_) return Result::unavailable;
    if (current_.state!=State::idle) return Result::busy;
    const auto available=capabilities();
    const auto result=probe(available,request);
    if (result!=Result::ok) return result;
    if (++next_==0) ++next_;
    current_={next_,State::ready,0,0,50};
    const bool has_audio=formats[static_cast<uint8_t>(request.format)].kind==Kind::audio || request.has_audio;
    if (!backend_->open(request,next_,!has_audio || !available.audio || !available.audio_output)) {
      current_.state=State::error;
      return Result::backend_failed;
    }
    return Result::ok;
  }

Result Playback::play(Token token) {
    if (!valid(token)) return Result::stale;
    if (current_.state==State::error) return Result::backend_failed;
    if (!backend_->play(token)) return fail();
    current_.state=State::playing; return Result::ok;
  }

Result Playback::pause(Token token) {
    if (!valid(token)) return Result::stale;
    if (current_.state==State::error) return Result::backend_failed;
    if (!capabilities().pause) return Result::unsupported_control;
    if (!backend_->pause(token)) return fail();
    current_.state=State::paused; return Result::ok;
  }

Result Playback::seek(Token token,double seconds) {
    if (!valid(token)) return Result::stale;
    if (current_.state==State::error) return Result::backend_failed;
    if (!std::isfinite(seconds) || seconds<0 || !current_.duration) return Result::invalid;
    if (!capabilities().seek) return Result::unsupported_control;
    if (seconds>current_.duration) seconds=current_.duration;
    if (!backend_->seek(token,seconds)) return fail();
    current_.position=seconds; return Result::ok;
  }

Result Playback::volume(Token token,int percent) {
    if (!valid(token)) return Result::stale;
    if (current_.state==State::error) return Result::backend_failed;
    if (!capabilities().volume || !capabilities().audio || !capabilities().audio_output) return Result::unsupported_control;
    if (percent<0 || percent>100) return Result::invalid;
    if (!backend_->volume(token,static_cast<uint8_t>(percent))) return fail();
    current_.volume=static_cast<uint8_t>(percent); return Result::ok;
  }

Result Playback::stop(Token token) {
    if (!valid(token)) return Result::stale;
    if (!backend_->stop(token)) return fail();
    current_={}; return Result::ok;
  }

bool Playback::report(Token token,State state,double position,double duration) {
    if (!valid(token) || state==State::idle || state>State::error || !std::isfinite(position) || !std::isfinite(duration) || position<0 || duration<0 || position>duration) return false;
    if (current_.state==State::error && state!=State::error) return false;
    current_.state=state; current_.position=position; current_.duration=duration;
    return true;
  }
}
