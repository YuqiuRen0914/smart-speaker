#include "lunaros_app_session.h"
#include "application_abi_contract.h"
#include <algorithm>
#include <cstring>
namespace lunaredu::lunaros::apps {
namespace {
bool leb(const uint8_t *p, size_t end, size_t &at, uint32_t &v) {
  v = 0;
  for (int i = 0; i < 5 && at < end; i++) {
    uint8_t b = p[at++];
    if (i == 4 && (b & 0xf0))
      return false;
    v |= uint32_t(b & 127) << (7 * i);
    if (!(b & 128))
      return true;
  }
  return false;
}
bool name(const uint8_t *p, size_t end, size_t &at, const char *const *allowed,
          size_t count,const char **matched=nullptr) {
  uint32_t n = 0;
  if (!leb(p, end, at, n) || n > end - at)
    return false;
  bool yes = false;
  for (size_t i = 0; i < count; i++)
    if (strlen(allowed[i]) == n && !memcmp(p + at, allowed[i], n)) {
      yes = true;
      if(matched)*matched=allowed[i];
    }
  at += n;
  return yes;
}
} // namespace
bool validate_module(const uint8_t *p, size_t size, uint32_t pages,bool *uses_device_basic) {
  if(uses_device_basic)*uses_device_basic=false;
  if (!p || size < 8 || size > 192 * 1024 || memcmp(p, "\0asm\1\0\0\0", 8) ||
      !pages || pages > 4)
    return false;
  size_t at = 8;
  bool memory = false;
  while (at < size) {
    uint8_t s = p[at++];
    uint32_t n = 0;
    if (!leb(p, size, at, n) || n > size - at || s == 8)
      return false;
    size_t end = at + n;
    if (s == 2) {
      uint32_t count = 0;
      if (!leb(p, end, at, count) || count > abi::import_count)
        return false;
      const char *modules[]{LUNAROS_APP_ABI_MODULE};
#define LUNA_IMPORT_NAME(name,method,signature,parameters,device,args) #name,
      const char *names[]{LUNAROS_APP_ABI_IMPORTS(LUNA_IMPORT_NAME)};
#undef LUNA_IMPORT_NAME
      for (uint32_t i = 0; i < count; i++) {
        uint32_t type = 0;
        const char *import=nullptr;
        if (!name(p, end, at, modules, 1) || !name(p, end, at, names, abi::import_count,&import) ||
            at == end || p[at++] != 0 || !leb(p, end, at, type))
          return false;
        for(const auto &entry:abi::imports)
          if(uses_device_basic&&entry.device&&import&&!std::strcmp(import,entry.name))*uses_device_basic=true;
      }
      if (at != end)
        return false;
    } else if (s == 4) {
      uint32_t count = 0, flags = 0, initial = 0, maximum = 0;
      if (!leb(p, end, at, count) || count > 1)
        return false;
      if (count &&
          (at == end || p[at++] != 0x70 || !leb(p, end, at, flags) ||
           flags != 1 || !leb(p, end, at, initial) ||
           !leb(p, end, at, maximum) || initial > maximum || maximum > 64))
        return false;
      if (at != end)
        return false;
    } else if (s == 5) {
      uint32_t count = 0, flags = 0, initial = 0, maximum = 0;
      if (memory || !leb(p, end, at, count) || count != 1 ||
          !leb(p, end, at, flags) || flags != 1 || !leb(p, end, at, initial) ||
          !leb(p, end, at, maximum) || !initial || initial > maximum ||
          maximum > pages || at != end)
        return false;
      memory = true;
    } else if (s == 7) {
      uint32_t count = 0;
#define LUNA_EXPORT_NAME(name,kind,parameters,required) #name,
      const char *exports[]{LUNAROS_APP_ABI_EXPORTS(LUNA_EXPORT_NAME)};
#undef LUNA_EXPORT_NAME
      if (!leb(p, end, at, count) || count > abi::export_count)
        return false;
      for (uint32_t i = 0; i < count; i++) {
        uint32_t index = 0;
        const char *export_name=nullptr;
        if (!name(p, end, at, exports, abi::export_count,&export_name) || at == end)
          return false;
        uint8_t kind = p[at++];
        if ((kind != 0 && kind != 2) || !leb(p, end, at, index))
          return false;
        for(const auto &entry:abi::exports)
          if(!std::strcmp(export_name,entry.name)&&kind!=entry.kind)return false;
      }
      if (at != end)
        return false;
    }
    at = end;
  }
  return memory;
}
} // namespace lunaredu::lunaros::apps
