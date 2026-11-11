#include "interaction/transport/lunarlink_protocol.h"
#include "interaction/transport/network_xiaozhi_protocol.h"
#include "interaction/transport/network_xiaozhi_mqtt_udp_channel.h"
#include <cassert>
#include <iostream>
#include <limits>

std::string json(JsonDocument &d){std::string s;serializeJson(d,s);return s;}
JsonDocument parse(const std::string &s){JsonDocument d;assert(deserializeJson(d,s)==DeserializationError::Ok);return d;}
std::string server_audio(uint32_t seq, const std::vector<uint8_t> &pcm) {
  // Independent reference framing and crypto: no production packet helpers.
  std::string out(32+pcm.size(),0);auto *p=reinterpret_cast<uint8_t*>(out.data());
  p[0]=2;p[1]=1;p[2]=pcm.size()>>8;p[3]=pcm.size();p[4]=0x12;p[5]=0x34;p[6]=0x56;p[7]=0x78;
  for(int i=0;i<4;++i)p[12+i]=seq>>(24-i*8);
  const uint8_t key[16]={};uint8_t iv[12]={0x12,0x34,0x56,0x78,0,0,0,1};
  for(int i=0;i<4;++i)iv[8+i]=seq>>(24-i*8);
  auto *ctx=EVP_CIPHER_CTX_new();int n=0;
  assert(EVP_EncryptInit_ex(ctx,EVP_aes_128_gcm(),nullptr,key,iv));
  assert(EVP_EncryptUpdate(ctx,nullptr,&n,p,16));assert(EVP_EncryptUpdate(ctx,p+16,&n,pcm.data(),pcm.size()));
  assert(EVP_EncryptFinal_ex(ctx,p+16+n,&n));assert(EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_GET_TAG,16,p+out.size()-16));EVP_CIPHER_CTX_free(ctx);return out;
}
int main(){
  LunarLinkProtocol wire;wire.device_id="LUNAR-TEST";wire.binding_epoch=7;
  auto hello=parse(wire.open());assert(hello["type"]=="session.open"&&hello["profile"]=="activity");
  assert(hello["payload"]["media_offers"][0]["encoding"]=="opus");
  JsonDocument p;p["session_id"]="ses_test";p["binding_epoch"]=7;
  for(auto field:{"media","input_media"}){p[field]["encoding"]="opus";p[field]["sample_rate_hz"]=std::string(field)=="media"?24000:16000;p[field]["channels"]=1;p[field]["frame_duration_ms"]=20;}
  p["endpoint"]["host"]="voice.example";p["endpoint"]["port"]=9000;p["endpoint"]["encryption"]="aes-128-gcm";
  p["endpoint"]["key"]="00000000000000000000000000000000";p["endpoint"]["nonce"]="02000000123456780000000000000000";
  p["features"].to<JsonArray>().add("conversation.playback_ready");
  auto opened=wire.envelope("session.opened",{},p.as<JsonVariantConst>(),wire.open_request);
  JsonDocument normalized;std::string ready;
  assert(wire.decode(opened,{},normalized,ready));XiaozhiUdpHelloConfig config;
  assert(xiaozhi_parse_mqtt_server_hello(json(normalized),config)&&config.sample_rate==24000&&config.frame_duration_ms==20);
  wire.session_opened=false;
  auto bad=parse(opened);bad["payload"]["input_media"]["encoding"]="pcm16le";assert(!wire.decode(json(bad),{},normalized,ready));
  bad=parse(opened);bad["request_id"]="req_stale";assert(!wire.decode(json(bad),{},normalized,ready));
  bad=parse(opened);bad["payload"]["binding_epoch"]=6;assert(!wire.decode(json(bad),{},normalized,ready));
  assert(wire.decode(opened,{},normalized,ready));
  assert(!wire.decode(opened,"ses_test",normalized,ready));
  auto start=parse(wire.encode_intent(xiaozhi_build_listen_start_message("ses_test","manual"),5));
  const std::string first=wire.activity_id;assert(start["payload"]["arguments"]["first_sequence"]==6);
  auto stop=parse(wire.encode_intent(xiaozhi_build_listen_stop_message("ses_test"),9));assert(stop["profile"]=="media"&&stop["payload"]["last_sequence"]==9);
  JsonDocument progress;progress["activity_id"]=first;progress["phase"]="playback_preparing";progress["result"]["first_audio_sequence"]=10;
  auto message=wire.envelope("activity.progress","ses_test",progress.as<JsonVariantConst>());
  assert(wire.decode(message,"ses_test",normalized,ready)&&normalized["state"]=="start"&&!ready.empty());
  auto ack=parse(ready);assert(ack["payload"]["first_sequence"]==10);
  bad=parse(message);bad["session"]["binding_epoch"]=6;assert(!wire.decode(json(bad),"ses_test",normalized,ready));
  bad=parse(message);bad["session"]["id"]="ses_old";assert(!wire.decode(json(bad),"ses_test",normalized,ready));
  bad=parse(message);bad["profile"]="mqtt";assert(!wire.decode(json(bad),"ses_test",normalized,ready));
  wire.encode_intent(xiaozhi_build_listen_start_message("ses_test","manual"),9);assert(wire.activity_id!=first);
  assert(!wire.decode(message,"ses_test",normalized,ready));
  progress.clear();progress["activity_id"]=wire.activity_id;progress["result"]["last_audio_sequence"]=12;
  auto complete=wire.envelope("activity.completed","ses_test",progress.as<JsonVariantConst>());
  assert(wire.decode(complete,"ses_test",normalized,ready)&&normalized["state"]=="stop");assert(!wire.decode(complete,"ses_test",normalized,ready));
  wire.first_downlink=10;wire.expected_last_downlink=12;wire.last_played_sequence=12;wire.played_packets=3;
  assert(wire.playback_delivered(true,false,false));
  assert(!wire.playback_delivered(true,true,false));
  assert(!wire.playback_delivered(false,false,false));
  wire.played_packets=2;assert(!wire.playback_delivered(true,false,false));
  wire.last_played_sequence=11;auto receipt=parse(wire.playback_receipt("ses_test",false));assert(receipt["payload"]["delivered"]==false);
  wire.encode_intent(xiaozhi_build_abort_message("ses_test"),9);assert(wire.activity_id.empty());
  assert(!wire.decode(complete,"ses_test",normalized,ready));

  JsonDocument closed;closed.to<JsonObject>();
  const auto goodbye=wire.envelope("session.closed","ses_test",closed.as<JsonVariantConst>());
  assert(wire.decode(goodbye,"ses_test",normalized,ready)&&normalized["type"]=="goodbye");
  XiaozhiMqttUdpChannel channel;
  XiaozhiMqttUdpConfig c{"voice.example",9000,"00000000000000000000000000000000","02000000123456780000000000000000"};
  assert(channel.configure(c));std::string packet;const uint8_t bytes[]={1,2,3,4};
  assert(channel.encrypt_payload(bytes,4,40,packet)&&packet.size()==36&&uint8_t(packet[15])==1&&packet[1]==0);
  assert(channel.configure(c));assert(channel.encrypt_payload(bytes,4,60,packet)&&uint8_t(packet[15])==2);
  auto equivalent=c;equivalent.nonce_hex.back()='1';equivalent.server="another.example";
  assert(channel.configure(equivalent));
  assert(channel.encrypt_payload(bytes,4,80,packet)&&uint8_t(packet[15])==3);
  auto server=server_audio(10,{1,2,3,4});auto tampered=server;tampered.back()^=1;
  auto decode=[&](const std::string &v){return channel.decrypt_payload(reinterpret_cast<const uint8_t*>(v.data()),v.size());};
  assert(decode(tampered).status==XiaozhiMqttUdpDecodeStatus::CryptoError);
  auto r=decode(server);assert(r.status==XiaozhiMqttUdpDecodeStatus::SequenceGap&&r.payload==std::vector<uint8_t>({1,2,3,4}));
  assert(decode(server).status==XiaozhiMqttUdpDecodeStatus::StaleSequence);
  assert(decode(server_audio(11,{5})).status==XiaozhiMqttUdpDecodeStatus::Ok);
  server=server_audio(12,{5});server[4]^=1;assert(decode(server).status==XiaozhiMqttUdpDecodeStatus::InvalidEnvelope);
  assert(decode(server_audio(12,{5})).status==XiaozhiMqttUdpDecodeStatus::Ok);
  assert(!channel.encrypt_payload(bytes,1469,0,packet));
  c.nonce_hex[0]='1';assert(!channel.configure(c));
  channel.reset();assert(!channel.ready());assert(host_keys().empty());
  std::cout<<"LunarLink: session/epoch/turn fences, negotiated Opus20, receipts, authenticated UDP tamper/replay and nonce continuity passed\n";
}
