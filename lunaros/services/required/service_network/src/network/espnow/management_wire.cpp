#include "lunaros_network_espnow_management_wire.h"
#include "lunaros_network_espnow_management.h"
#include <cstring>
namespace lunaredu::lunaros {
namespace {
void put(uint8_t *out,uint64_t value,size_t count){for(size_t i=0;i<count;++i)out[i]=uint8_t(value>>(i*8));}
uint64_t get(const uint8_t *in,size_t count){uint64_t value=0;for(size_t i=0;i<count;++i)value|=uint64_t(in[i])<<(i*8);return value;}
}
bool espnow_encode_management(const EspNowManagementEnvelope &value,uint8_t *out,size_t &size){
  const size_t required=kEspNowManagementHeaderBytes+value.size;
  if(!out||value.size>kEspNowManagementPayloadBytes||size<required||(!value.payload&&value.size)||
      value.flags>2||value.family>3||!value.nonce)return false;
  std::memcpy(out,"LEM1",4);out[4]=value.family;out[5]=value.flags;out[6]=out[7]=0;
  put(out+8,value.nonce,8);put(out+16,value.sequence,4);
  if(value.size)std::memcpy(out+20,value.payload,value.size);
  size=required;return true;
}
bool espnow_decode_management(const uint8_t *data,size_t size,EspNowManagementEnvelope &value){
  value={};
  if(!data||size<20||size>20+kEspNowManagementPayloadBytes||std::memcmp(data,"LEM1",4)||
      data[4]>3||data[5]>2||data[6]||data[7])return false;
  value={data[4],data[5],get(data+8,8),uint32_t(get(data+16,4)),data+20,size-20};
  return value.nonce!=0;
}
}
