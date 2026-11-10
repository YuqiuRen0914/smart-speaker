#include "lunaros_files_backend.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace lunaredu::lunaros::media {
namespace {
uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t u32(const uint8_t *p) { return uint32_t(u16(p)) | uint32_t(u16(p + 2)) << 16; }
}
bool JpegSequence::marker(uint8_t code) {
  if(code == 0xff) return true;
  if(code == 0xd9) {
    if(frames_.size() >= maximum_frames) return false;
    frames_.push_back({start_, offset_ + 1 - start_}); state_ = Parse::Prefix; return true;
  }
  if(code == 0 || code == 0xd8) return false;
  if(code == 1 || (code >= 0xd0 && code <= 0xd7)) { state_ = Parse::MarkerPrefix; return true; }
  marker_ = code; length_ = 0; state_ = Parse::LengthHigh; return true;
}
bool JpegSequence::feed(const uint8_t *bytes, size_t size) {
  if(failed_ || (!bytes && size)) return false;
  for(size_t i = 0; i < size; ++i, ++offset_) {
    const uint8_t byte = bytes[i]; bool ok = true;
    if(state_ != Parse::Prefix && offset_ - start_ >= maximum_frame_bytes) ok = false;
    switch(state_) {
      case Parse::Prefix: start_ = offset_; ok &= byte == 0xff; state_ = Parse::Soi; break;
      case Parse::Soi: ok &= byte == 0xd8; state_ = Parse::MarkerPrefix; break;
      case Parse::MarkerPrefix: ok &= byte == 0xff; state_ = Parse::Marker; break;
      case Parse::Marker: ok &= marker(byte); break;
      case Parse::LengthHigh: length_ = uint16_t(byte) << 8; state_ = Parse::LengthLow; break;
      case Parse::LengthLow:
        length_ |= byte; ok &= length_ >= 2; skip_ = length_ >= 2 ? length_ - 2 : 0;
        state_ = skip_ ? Parse::Skip : marker_ == 0xda ? Parse::Entropy : Parse::MarkerPrefix; break;
      case Parse::Skip: if(--skip_ == 0) state_ = marker_ == 0xda ? Parse::Entropy : Parse::MarkerPrefix; break;
      case Parse::Entropy: if(byte == 0xff) state_ = Parse::EntropyMarker; break;
      case Parse::EntropyMarker:
        if(byte == 0 || (byte >= 0xd0 && byte <= 0xd7)) state_ = Parse::Entropy;
        else ok &= marker(byte);
        break;
    }
    if(!ok) { failed_ = true; return false; }
  }
  return true;
}
bool JpegSequence::complete() const { return !failed_ && state_ == Parse::Prefix && !frames_.empty(); }
bool read_wave(FileIo io, const char *path, size_t bytes, WaveInfo &out) {
  out = {}; uint8_t header[24]{};
  if(!io.read || bytes < 44 || bytes > 32 * 1024 * 1024 || !io.read(io.context,path,0,header,12) ||
     std::memcmp(header,"RIFF",4) || std::memcmp(header+8,"WAVE",4) || u32(header+4) != bytes-8) return false;
  bool fmt = false, data = false; size_t cursor = 12;
  for(unsigned count = 0; cursor + 8 <= bytes && count < 64; ++count) {
    if(!io.read(io.context,path,cursor,header,8)) return false;
    const size_t length = u32(header+4), body = cursor+8;
    if(length > bytes-body) return false;
    if(!std::memcmp(header,"fmt ",4)) {
      if(fmt || length < 16 || !io.read(io.context,path,body,header,16) || u16(header)!=1 || u16(header+14)!=16) return false;
      out.channels=u16(header+2); out.sample_rate=u32(header+4);
      if(out.channels<1 || out.channels>2 || out.sample_rate<8000 || out.sample_rate>48000 ||
         u16(header+12)!=out.channels*2 || u32(header+8)!=out.sample_rate*out.channels*2) return false;
      fmt = true;
    } else if(!std::memcmp(header,"data",4)) {
      if(data) return false;
      out.data_offset=static_cast<uint32_t>(body); out.data_bytes=static_cast<uint32_t>(length); data=true;
    }
    if(length + (length&1) > bytes-body) return false;
    cursor=body+length+(length&1);
  }
  return cursor==bytes && fmt && data && out.data_bytes && out.data_bytes % (out.channels*2)==0;
}
Capabilities FileBackend::capabilities() const {
  const bool audio=audio_ && audio_->ready();
  return {audio,io_.jpeg!=nullptr,audio,true,true,audio,
      (audio?format_bit(Format::wav_pcm16):0) | (io_.jpeg?format_bit(Format::mjpeg):0),240,240};
}
bool FileBackend::fail() { state_.state=State::error; return false; }
bool FileBackend::open(const Request &r, Token token, bool silent) {
  if(token_ || !desktop::file_user_path(r.path?r.path:"") || !io_.read) return false;
  path_=r.path; token_=token; format_=r.format; state_={token,State::ready,0,0,50};
  scan_=submitted_=seek_frame_=0; clock_ready_=false; audible_=false;
  if(format_==Format::wav_pcm16) {
    if(silent || !audio_ || !audio_->ready() || !read_wave(io_,path_.c_str(),size_,wave_)) return fail();
    // A failed start may retain an audio lease; stop must still clean it up.
    audible_=true;
    if(!audio_->start(wave_.sample_rate) || !audio_->volume(50)) return fail();
    state_.duration=double(wave_.data_bytes)/(wave_.channels*2*wave_.sample_rate);
  } else if(format_==Format::mjpeg) {
    if(!io_.jpeg || !size_ || size_>8*1024*1024) return fail();
    sequence_=JpegSequence{}; indexing_=true;
  } else return fail();
  return true;
}
bool FileBackend::play(Token token) {
  if(token!=token_ || indexing_ || state_.state==State::error) return false;
  if(state_.state==State::ended && !seek(token,0)) return false;
  state_.state=State::playing; clock_ready_=false; return true;
}
bool FileBackend::pause(Token token) {
  if(token!=token_ || state_.state==State::error) return false;
  if(audible_) {
    const size_t completed=std::min<uint64_t>(audio_->completed(),submitted_);
    seek_frame_+=completed; submitted_=0;
    if(!audio_->flush()) return fail();
    state_.position=double(seek_frame_)/wave_.sample_rate;
  }
  state_.state=State::paused; clock_ready_=false; return true;
}
bool FileBackend::seek(Token token,double seconds) {
  if(token!=token_ || indexing_ || !std::isfinite(seconds) || seconds<0 || seconds>state_.duration) return false;
  if(audible_) {
    if(!audio_->flush()) return fail();
    seek_frame_=std::min<size_t>(wave_.data_bytes/(wave_.channels*2),static_cast<size_t>(seconds*wave_.sample_rate)); submitted_=0;
  } else {
    const size_t index=std::min(sequence_.frames().size()-1,static_cast<size_t>(seconds*10));
    if(!decode(index)) return fail();
  }
  state_.position=seconds;
  if(state_.state==State::ended)state_.state=State::paused;
  clock_ready_=false; return true;
}
bool FileBackend::volume(Token token,uint8_t percent) {
  if(token!=token_ || !audible_ || percent>100 || !audio_->volume(percent)) return false;
  state_.volume=percent; return true;
}
bool FileBackend::stop(Token token) {
  if(token!=token_) return false;
  if(audible_ && !audio_->stop()) return fail();
  token_=0; audible_=indexing_=false; image_.reset(); path_.clear(); sequence_=JpegSequence{}; state_={}; return true;
}
bool FileBackend::decode(size_t index) {
  if(index>=sequence_.frames().size()) return false;
  const auto frame=sequence_.frames()[index]; std::vector<uint8_t> jpeg(frame.size);
  auto image=std::make_shared<desktop::FilePixels>();
  if(!io_.read(io_.context,path_.c_str(),frame.offset,jpeg.data(),jpeg.size()) ||
      !io_.jpeg(io_.context,jpeg.data(),jpeg.size(),*image) || !image->width || !image->height ||
      image->width>240 || image->height>240 || image->pixels.size()!=size_t(image->width)*image->height) return false;
  image_=std::move(image); return true;
}
bool FileBackend::tick(uint32_t now) {
  if(!token_ || state_.state==State::error) return false;
  if(indexing_) {
    const size_t count=std::min(scratch_.size(),size_-scan_);
    if(!io_.read(io_.context,path_.c_str(),scan_,scratch_.data(),count) || !sequence_.feed(scratch_.data(),count)) { fail(); return true; }
    scan_+=count;
    if(scan_==size_) {
      indexing_=false;
      if(!sequence_.complete() || !decode(0)) { fail(); return true; }
      state_.duration=sequence_.frames().size()/10.;
      return true;
    }
    return false;
  }
  if(state_.state!=State::playing) return false;
  if(audible_) {
    const size_t total=wave_.data_bytes/(wave_.channels*2);
    const size_t finished=std::min<uint64_t>(audio_->completed(),submitted_);
    state_.position=std::min(state_.duration,double(seek_frame_+finished)/wave_.sample_rate);
    if(seek_frame_+finished>=total) { state_.state=State::ended; return true; }
    const size_t count=std::min<size_t>(512,total-seek_frame_-submitted_);
    if(count) {
      if(!io_.read(io_.context,path_.c_str(),wave_.data_offset+(seek_frame_+submitted_)*wave_.channels*2,scratch_.data(),count*wave_.channels*2)) { fail();return true; }
      for(size_t n=0;n<count;++n) {
        int32_t value=static_cast<int16_t>(u16(scratch_.data()+n*wave_.channels*2));
        if(wave_.channels==2) value=(value+static_cast<int16_t>(u16(scratch_.data()+n*4+2)))/2;
        samples_[n]=static_cast<int16_t>(value);
      }
      if(audio_->write(samples_.data(),count)) submitted_+=count;
    }
    return true;
  }
  if(!clock_ready_) { clock_=now; clock_position_=state_.position; clock_ready_=true; }
  const double position=std::min(state_.duration,clock_position_+uint32_t(now-clock_)/1000.);
  const size_t index=std::min(sequence_.frames().size()-1,static_cast<size_t>(position*10));
  if(index!=std::min(sequence_.frames().size()-1,static_cast<size_t>(state_.position*10)) && !decode(index)) { fail();return true; }
  const bool changed=index!=static_cast<size_t>(state_.position*10) || position>=state_.duration;
  state_.position=index/10.;
  if(position>=state_.duration) { state_.position=state_.duration; state_.state=State::ended; }
  return changed;
}
} // namespace lunaredu::lunaros::media
