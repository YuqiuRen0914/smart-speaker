#include "lunaros_micropython_app_backend.h"
namespace lunaredu::micropython {
bool validate_application_source(const uint8_t *p, size_t n) {
  if (!p || !n || n > 12288) return false;
  // Reject native .mpy code, embedded NULs and invalid UTF-8 before admission.
  for (size_t i=0; i<n;) {
    uint32_t ch=p[i++]; if (!ch) return false; if (ch<128) continue;
    unsigned count; uint32_t minimum;
    if(ch>=0xc2&&ch<=0xdf){count=1;minimum=0x80;ch&=31;}
    else if(ch>=0xe0&&ch<=0xef){count=2;minimum=0x800;ch&=15;}
    else if(ch>=0xf0&&ch<=0xf4){count=3;minimum=0x10000;ch&=7;}
    else return false;
    if(n-i<count) return false;
    while(count--){const auto c=p[i++];if((c&0xc0)!=0x80)return false;ch=(ch<<6)|(c&63);}
    if(ch<minimum||ch>0x10ffff||(ch>=0xd800&&ch<=0xdfff))return false;
  }
  return true;
}
}
