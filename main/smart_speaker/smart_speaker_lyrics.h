#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace smart_speaker::lyrics {
constexpr size_t kMaxBytes = 32768;
constexpr size_t kMaxCues = 512;
struct Cue { uint32_t ms; std::string text; };
struct Document {
  std::vector<Cue> cues;
  int offset_ms = 0;
  const std::string &line(uint32_t position_ms, int delay_ms = 0) const {
    static const std::string empty;
    const int64_t time = int64_t(position_ms) + offset_ms - delay_ms;
    auto it = std::upper_bound(cues.begin(), cues.end(), time,
        [](int64_t t, const Cue &cue) { return t < cue.ms; });
    return it == cues.begin() ? empty : std::prev(it)->text;
  }
};
inline bool utf8(std::string_view text) {
  for (size_t i = 0; i < text.size();) {
    const auto c = static_cast<uint8_t>(text[i++]);
    if (c < 0x80) { if (c < 32 && c != '\n' && c != '\r' && c != '\t') return false; continue; }
    unsigned n; uint32_t cp, minimum;
    if (c >= 0xc2 && c <= 0xdf) { n=1; cp=c&31; minimum=0x80; }
    else if (c >= 0xe0 && c <= 0xef) { n=2; cp=c&15; minimum=0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { n=3; cp=c&7; minimum=0x10000; }
    else return false;
    while (n--) { if (i == text.size()) return false; const auto d=uint8_t(text[i++]);
      if ((d&0xc0)!=0x80) return false;
      cp=(cp<<6)|(d&63); }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
  }
  return true;
}
inline bool number(std::string_view s, uint32_t &n) {
  if (s.empty() || s.size() > 7) return false;
  n=0; for (char c : s) { if (c<'0'||c>'9') return false; n=n*10+uint32_t(c-'0'); }
  return true;
}
inline bool timestamp(std::string_view s, uint32_t &ms) {
  const auto colon=s.find(':'); if (colon==s.npos) return false;
  const auto dot=s.find('.',colon+1);
  uint32_t minutes=0, seconds=0, fraction=0;
  if (!number(s.substr(0,colon),minutes) || minutes>1440 ||
      !number(s.substr(colon+1, dot==s.npos?s.size()-colon-1:dot-colon-1),seconds) || seconds>=60) return false;
  if (dot!=s.npos) {
    const auto f=s.substr(dot+1); if (f.size()>3 || !number(f,fraction)) return false;
    if (f.size()==1) fraction*=100; else if (f.size()==2) fraction*=10;
  }
  ms=(minutes*60+seconds)*1000+fraction; return true;
}
inline bool parse(std::string_view input, Document &out, std::string &error) {
  out={}; error.clear();
  auto fail=[&](const char *why) { out={}; error=why; return false; };
  if (input.empty() || input.size()>kMaxBytes) return fail("lyrics_size_invalid");
  if (!utf8(input)) return fail("lyrics_utf8_required");
  if (input.substr(0,3)=="\xef\xbb\xbf") input.remove_prefix(3);
  size_t text_bytes = 0;
  while (!input.empty()) {
    auto end=input.find('\n'); auto row=input.substr(0,end);
    input=end==input.npos?std::string_view{}:input.substr(end+1);
    if (!row.empty() && row.back()=='\r') row.remove_suffix(1);
    if (row.substr(0,8)=="[offset:") {
      if (row.back()!=']') return fail("lyrics_offset_invalid");
      auto value=row.substr(8,row.size()-9); bool negative=!value.empty()&&value.front()=='-';
      if (!value.empty()&&(value.front()=='-'||value.front()=='+')) value.remove_prefix(1);
      uint32_t n=0; if (!number(value,n)||n>600000) return fail("lyrics_offset_invalid");
      out.offset_ms=negative?-int(n):int(n); continue;
    }
    std::vector<uint32_t> times;
    while (!row.empty() && row.front()=='[') {
      auto close=row.find(']'); uint32_t t=0;
      if (close==row.npos || !timestamp(row.substr(1,close-1),t)) break;
      if (times.size()+out.cues.size()>=kMaxCues) return fail("lyrics_too_many_lines");
      times.push_back(t); row.remove_prefix(close+1);
    }
    if (times.empty()) continue;
    if (row.size()>512) return fail("lyrics_line_too_long");
    text_bytes += row.size()*times.size();
    if (text_bytes > kMaxBytes*2) return fail("lyrics_size_invalid");
    for (auto t:times) out.cues.push_back({t,std::string(row)});
  }
  if (out.cues.empty()) return fail("lyrics_timestamps_required");
  std::stable_sort(out.cues.begin(),out.cues.end(),[](const Cue &a,const Cue &b){return a.ms<b.ms;});
  // Duplicate timestamps (e.g. translations) share a single two-line caption.
  std::vector<Cue> merged;
  for (auto &cue:out.cues) {
    if (!merged.empty() && merged.back().ms==cue.ms) {
      if (!cue.text.empty()) {
        if (!merged.back().text.empty()) merged.back().text+='\n';
        merged.back().text+=cue.text;
        if (merged.back().text.size()>512) return fail("lyrics_line_too_long");
      }
    } else merged.push_back(std::move(cue));
  }
  out.cues=std::move(merged); return true;
}

// Count only successful song PCM submissions, never wall time or idle DMA.
// Carry fractional microseconds so 44.1 kHz does not drift over a long track.
class PcmClock {
 public:
  void reset() { microseconds_=remainder_=0; rate_=0; }
  void advance(uint32_t frames,uint32_t rate) {
    if (!rate) return;
    if (rate!=rate_) { remainder_=0; rate_=rate; }
    const uint64_t n=uint64_t(frames)*1000000+remainder_;
    microseconds_+=n/rate; remainder_=n%rate;
  }
  uint32_t milliseconds() const { return uint32_t(std::min<uint64_t>(microseconds_/1000,UINT32_MAX)); }
 private:
  uint64_t microseconds_=0, remainder_=0;
  uint32_t rate_=0;
};
} // namespace smart_speaker::lyrics
