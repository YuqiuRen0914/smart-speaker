#include "lunaredu_device_wire_envelope.h"

namespace lunaredu::protocol {
bool wire_id(std::string_view value, std::string_view prefix) {
  if (value.size() < prefix.size()+9 || value.size() > prefix.size()+65 ||
      value.substr(0,prefix.size()) != prefix || value[prefix.size()] != '_') return false;
  for (size_t i=prefix.size()+1;i<value.size();++i) {
    const auto c=value[i];
    if (!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')) return false;
  }
  return true;
}

bool operation_id(std::string_view value) {
  if(value.empty()||value.size()>80)return false;
  for(size_t i=0;i<value.size();++i) {
    const char c=value[i];
    if((c>='a'&&c<='z')||(c>='0'&&c<='9'))continue;
    if(i>0&&(c=='_'||c=='.'||c=='-'))continue;
    return false;
  }
  return true;
}

std::string json_quote(std::string_view value) {
  static constexpr char hex[]="0123456789abcdef";
  std::string out="\"";
  out.reserve(value.size()+2);
  for (unsigned char c:value) {
    if (c=='"'||c=='\\') {out+='\\';out+=static_cast<char>(c);}
    else if (c<0x20) {out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
    else out+=static_cast<char>(c);
  }
  out+='"';return out;
}

bool wire_timestamp(std::string_view value) {
  if (value.size()<20 || value[4]!='-' || value[7]!='-' || value[10]!='T' ||
      value[13]!=':' || value[16]!=':') return false;
  auto number=[&](size_t start,size_t count) {
    int n=0;
    for(size_t i=start;i<start+count;++i){
      if(value[i]<'0'||value[i]>'9')return -1;
      n=n*10+value[i]-'0';
    }
    return n;
  };
  const int y=number(0,4),m=number(5,2),d=number(8,2),hh=number(11,2),mm=number(14,2),ss=number(17,2);
  if(y<0||m<1||m>12||d<1||hh<0||hh>23||mm<0||mm>59||ss<0||ss>59)return false;
  const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
  if(d>days[m-1]+(m==2&&y%4==0&&(y%100!=0||y%400==0)?1:0))return false;
  size_t i=19;
  if(value[i]=='.') {const auto first=++i;while(i<value.size()&&value[i]>='0'&&value[i]<='9')++i;
    if(i==first||i-first>9||i>=value.size())return false;}
  if(value[i]=='Z')return i+1==value.size();
  return (value[i]=='+'||value[i]=='-')&&i+6==value.size()&&value[i+3]==':'&&
      number(i+1,2)>=0&&number(i+1,2)<=23&&number(i+4,2)>=0&&number(i+4,2)<=59;
}

bool valid_header(const MessageHeader &h) {
  return contract::message_type(h.type) && wire_id(h.message_id,"msg") &&
      wire_timestamp(h.sent_at) && (h.request_id.empty()||wire_id(h.request_id,"req")) &&
      (h.correlation_id.empty()||wire_id(h.correlation_id,"msg")||
       wire_id(h.correlation_id,"req")||wire_id(h.correlation_id,"act")||wire_id(h.correlation_id,"media")) &&
      (h.session_id.empty()||(wire_id(h.session_id,"ses")&&h.binding_epoch>=0));
}

std::string encode_message(const MessageHeader &h, std::string_view payload) {
  if (!valid_header(h)||payload.size()<2||payload.front()!='{'||payload.back()!='}') return {};
  std::string out="{\"protocol\":"+json_quote(contract::name)+",\"version\":1,\"family\":\"device\",\"profile\":"+json_quote(contract::profile_for(h.type))+",\"message_id\":"+json_quote(h.message_id)+
      ",\"type\":"+json_quote(h.type)+",\"sent_at\":"+json_quote(h.sent_at);
  if (!h.request_id.empty()) out+=",\"request_id\":"+json_quote(h.request_id);
  if (!h.correlation_id.empty()) out+=",\"correlation_id\":"+json_quote(h.correlation_id);
  if (!h.session_id.empty()) out+=",\"session\":{\"id\":"+json_quote(h.session_id)+
      ",\"binding_epoch\":"+std::to_string(h.binding_epoch)+"}";
  return out+",\"payload\":"+std::string(payload)+"}";
}

std::string device_session_id(std::string_view serial) {
  std::string id="ses_";
  for(char c:serial) id+=c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):c;
  return id;
}
}
