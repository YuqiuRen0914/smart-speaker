#include "lunaredu_qr_payload.h"

namespace lunaredu::lunaros::qr {
namespace {
std::string escape_wifi_qr(std::string_view value) {
  std::string escaped;
  for(char c:value){if(c=='\\'||c==';'||c==','||c==':'||c=='"')escaped+='\\';escaped+=c;}
  return escaped;
}
}
std::string wifi_payload(std::string_view ssid, std::string_view password) {
    if(ssid.empty()||ssid.size()>32||ssid.find('\0')!=std::string_view::npos||
        password.size()>63||password.find('\0')!=std::string_view::npos)return {};
    return "WIFI:T:"+std::string(password.empty()?"nopass":"WPA")+";S:"+
        escape_wifi_qr(ssid)+";P:"+escape_wifi_qr(password)+";;";
}
}
