#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lunaredu::music::lyrics {
constexpr size_t kMaxBytes = 32768;
constexpr size_t kMaxCues = 512;
struct Cue { uint32_t ms; std::string text; };
struct Document {
  std::vector<Cue> cues;
  int offset_ms = 0;
  const std::string &line(uint32_t position_ms,int delay_ms=0) const;
};
bool parse(std::string_view,Document &,std::string &error);
std::string path_for(const std::string &track);
bool exists(const std::string &path);
std::string readable(const std::string &path);
bool save(const std::string &path,const std::string &text);
std::shared_ptr<const Document> load(const std::string &track);
int delay(const std::string &track);
} // namespace lunaredu::music::lyrics
