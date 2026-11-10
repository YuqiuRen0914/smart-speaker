#include "transfer_internal.h"
#include <algorithm>
#include "lunaros_audio_import.h"
#include <cstdio>
#include <cstring>
#include <cctype>
namespace smart_speaker {
using namespace lunaredu::lunaros;
namespace detail {
std::string lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}
bool ends_with_mp3(const std::string &name) {
  const std::string lower = lower_copy(name);
  return lower.size() > 4 &&
         lower.compare(lower.size() - 4, 4, ".mp3") == 0;
}
constexpr size_t kMaxUploadNameLength=96;
TransferStatus g_transfer;
std::mutex g_transfer_mutex;
bool safe_upload_name(const std::string &name) {
  if (name.empty() || name.size() > kMaxUploadNameLength ||
      !ends_with_mp3(name)) {
    return false;
  }
  for (unsigned char ch : name) if (ch < 32 || ch == 127) return false;
  return name.find('/') == std::string::npos &&
         name.find('\\') == std::string::npos &&
         name.find_first_of(":*?\"<>|") == std::string::npos &&
         name.find("..") == std::string::npos;
}

bool stream_upload_to_file(const DeviceBulkRequest &request,const std::string &path,
                           size_t &written,std::string &error) {
  struct Input { const DeviceBulkRequest &request;size_t offset=0; } input{request};
  lunaredu::audio::AudioImportStream stream;
  stream.context=&input;
  stream.size=request.body_data?request.body_size:request.content_length;
  stream.read=[](void *context,uint8_t *out,size_t wanted){
    auto &input=*static_cast<Input *>(context);const auto &r=input.request;
    if(r.body_data){const size_t n=std::min(wanted,r.body_size-input.offset);
      std::memcpy(out,r.body_data+input.offset,n);input.offset+=n;return int(n);}
    return r.read_chunk?r.read_chunk(out,wanted,r.context):-1;
  };
  stream.progress=[](void *,size_t count){std::lock_guard<std::mutex> lock(g_transfer_mutex);g_transfer.received=count;};
  return lunaredu::audio::import_mp3_file(stream,path,written,error);
}
}
TransferStatus transfer_snapshot() {
  std::lock_guard<std::mutex> lock(detail::g_transfer_mutex);return detail::g_transfer;
}
}
