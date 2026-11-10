#pragma once
// Generated from protocol/wire/v1/contract.json. Do not edit.
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <initializer_list>
#include <limits>
namespace lunaredu::protocol::profiles {
inline constexpr const char *remote_protocol="lunaredu.wire";
inline constexpr const char *remote_radio_transport="espressif-espnow-data-v2";
inline constexpr const char *provisioning_protocol="network-provisioning-security1";
inline constexpr const char *provisioning_session_endpoint="prov-session";
inline constexpr const char *provisioning_config_endpoint="prov-config";
inline constexpr const char *provisioning_proof_prefix="LP/1:device:";
inline constexpr const char *serial_protocol="lunaredu.wire";
inline constexpr const char *serial_read_type="serial.read";
inline constexpr const char *serial_ack_ok=R"ack({"type":"serial.write.result","ok":true,"result":{}})ack";
inline constexpr const char *serial_ack_busy=R"ack({"type":"serial.write.result","ok":false,"result":{},"error":{"code":"serial_busy"}})ack";
inline constexpr size_t serial_header_bytes=24,serial_max_payload_bytes=4096,serial_max_tx_bytes=1024;
inline constexpr uint8_t serial_version=1;
inline constexpr size_t fragment_max_parts=256,fragment_max_part_bytes=512,fragment_max_response_bytes=18432;
inline constexpr int64_t fragment_timeout_ms=10000;
inline constexpr uint8_t media_version=2;
inline constexpr size_t media_header_bytes=16,media_tag_bytes=16,media_max_payload_bytes=1468;
inline constexpr const char *media_encryption="aes-128-gcm";
inline bool parse_u64(std::string_view text,uint64_t &value) {
  value=0;if(text.empty()||text.size()>20||(text.size()>1&&text[0]=='0'))return false;
  for(char c:text){if(c<'0'||c>'9'||value>(std::numeric_limits<uint64_t>::max()-uint64_t(c-'0'))/10)return false;value=value*10+uint64_t(c-'0');}return true;
}
inline void write_le(uint8_t *out,uint64_t value,size_t bytes){for(size_t i=0;i<bytes;++i)out[i]=uint8_t(value>>(8*i));}
inline void serial_header(uint8_t *out,uint32_t generation,uint64_t cursor,uint64_t lost){out[0]=76;out[1]=83;out[2]=serial_version;out[3]=0;write_le(out+4,generation,4);write_le(out+8,cursor,8);write_le(out+16,lost,8);}
inline uint32_t read_be(const uint8_t *p,size_t n){uint32_t v=0;for(size_t i=0;i<n;i++)v=(v<<8)|p[i];return v;}
inline void write_be(uint8_t *p,uint32_t v,size_t n){for(size_t i=0;i<n;i++)p[n-i-1]=uint8_t(v>>(8*i));}
inline bool media_header(uint8_t *out,size_t payload,uint32_t connection,uint32_t timestamp,uint32_t sequence,uint8_t direction){if(!payload||payload>media_max_payload_bytes||!sequence||direction>1)return false;out[0]=media_version;out[1]=direction;write_be(out+2,payload,2);write_be(out+4,connection,4);write_be(out+8,timestamp,4);write_be(out+12,sequence,4);return true;}
inline bool media_valid(const uint8_t *p,size_t size,uint8_t direction){return p&&size>=media_header_bytes+media_tag_bytes+1&&size<=media_header_bytes+media_tag_bytes+media_max_payload_bytes&&p[0]==media_version&&p[1]==direction&&read_be(p+12,4)>0&&size==media_header_bytes+media_tag_bytes+read_be(p+2,2);}
inline void media_nonce(const uint8_t *header,uint8_t *nonce){for(size_t i=0;i<12;++i)nonce[i]=0;for(size_t i=0;i<4;++i){nonce[i]=header[4+i];nonce[8+i]=header[12+i];}nonce[7]=header[1];}
inline bool auth_decimal(std::string_view text){uint64_t value=0;return parse_u64(text,value)&&value<=9007199254740991ULL;}
inline bool auth_hex(std::string_view text,size_t length){if(text.size()!=length)return false;for(char c:text)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
inline constexpr const char *auth_owner="llo1";
inline constexpr int auth_owner_ttl=120;
inline constexpr int auth_owner_clock_skew=60;
inline constexpr const char *auth_shared="lls1";
inline constexpr int auth_shared_ttl=5;
inline constexpr int auth_shared_clock_skew=0;
inline constexpr const char *auth_admin="lla1";
inline constexpr int auth_admin_ttl=120;
inline constexpr int auth_admin_clock_skew=60;
inline constexpr const char *auth_admin_request="llr1";
inline constexpr int auth_admin_request_ttl=600;
inline constexpr int auth_admin_request_clock_skew=60;
std::string auth_message_owner(std::string_view serial,std::string_view binding_epoch,std::string_view access_epoch,std::string_view actor,std::string_view expires,std::string_view audience);
std::string auth_message_shared(std::string_view serial,std::string_view binding_epoch,std::string_view access_epoch,std::string_view actor,std::string_view expires,std::string_view audience);
std::string auth_message_admin(std::string_view serial,std::string_view binding_epoch,std::string_view expires,std::string_view audience);
std::string auth_message_admin_request(std::string_view serial,std::string_view binding_epoch,std::string_view expires,std::string_view state,std::string_view callback_origin,std::string_view audience);
inline constexpr const char *mqtt_voice_up="lunarlink/{device_id}/up",*mqtt_voice_down="lunarlink/{device_id}/down",*mqtt_control_up="lunaredu/hardware/v2/devices/{serial}/command-results",*mqtt_control_down="lunaredu/hardware/v2/devices/{serial}/commands/teleop";
std::string topic(std::string_view pattern,std::string_view id);
}
