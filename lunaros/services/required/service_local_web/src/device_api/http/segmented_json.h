#pragma once
#include <ArduinoJson.h>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <string_view>

namespace lunaredu::lunaros::internal {
// The transport owns its fixed staging buffer. Producers retain only one JSON
// fragment at a time; any allocation/short-write failure makes the stream fail.
struct JsonStreamSink {
  void *context=nullptr;
  size_t (*send)(void *,const uint8_t *,size_t)=nullptr;
  bool failed=false;
  size_t write(const uint8_t *data,size_t size) {
    if(failed||!send)return 0;
    const size_t count=send(context,data,size);failed=count!=size;return count;
  }
  size_t write(uint8_t byte){return write(&byte,1);}
  bool text(const char *value){const auto n=std::strlen(value);return write(reinterpret_cast<const uint8_t *>(value),n)==n;}
  bool json(JsonVariantConst value){return serializeJson(value,*this)>0&&!failed;}
};
inline bool write_json_object_prefix(JsonStreamSink &sink,const JsonDocument &document) {
  if(document.overflowed()||!document.is<JsonObjectConst>())return false;
  struct Prefix {
    JsonStreamSink &sink;size_t remaining;
    size_t write(const uint8_t *data,size_t size){
      const auto take=std::min(size,remaining);
      if(take&&sink.write(data,take)!=take)return 0;
      remaining-=take;return size; // Omit only the final closing brace.
    }
    size_t write(uint8_t byte){return write(&byte,1);}
  } writer{sink,measureJson(document)-1};
  return serializeJson(document,writer)>0&&!sink.failed&&!writer.remaining;
}
// No throwing std::string concatenation while a document is under pressure.
// JsonString explicitly asks ArduinoJson to copy before this scratch is freed.
inline bool set_joined_json_string(JsonObject target,const char *key,
    std::initializer_list<std::string_view> pieces) {
  if(target.isNull())return false;
  size_t size=0;for(auto piece:pieces){if(piece.size()>SIZE_MAX-1-size)return false;size+=piece.size();}
  std::unique_ptr<char[]> buffer(new(std::nothrow) char[size+1]);if(!buffer)return false;
  size_t at=0;for(auto piece:pieces){std::memcpy(buffer.get()+at,piece.data(),piece.size());at+=piece.size();}
  buffer[size]=0;
  return target[key].set(JsonString(buffer.get(),size));
}
}
