#include "character/project.h"
#include <cstring>
#include <utility>

namespace lunaredu::pet::character {
namespace {
bool range(JsonVariantConst value,float (&out)[2],bool positive) {
  if(!value.is<JsonArrayConst>()||value.size()!=2)return false;
  for(size_t i=0;i<2;++i){
    if(!value[i].is<uint32_t>()||value[i].as<uint32_t>()>60000)return false;
    out[i]=float(value[i].as<uint32_t>());
  }
  return out[0]<=out[1]&&(!positive||out[0]>0);
}
bool text_is(JsonVariantConst value,const char *expected){
  return value.is<const char *>()&&!std::strcmp(value.as<const char *>(),expected);
}
size_t characters(const char *text){size_t count=0;for(const auto *p=reinterpret_cast<const unsigned char *>(text);*p;++p)if((*p&0xc0)!=0x80)++count;return count;}
}
bool parse_project(const std::string &json,Project &out,std::string &error){
  const auto fail=[&](const char *why){error=why;return false;};
  if(json.empty()||json.size()>kMaximumProjectBytes)return fail("character_project_size");
  JsonDocument document;
  if(deserializeJson(document,json,DeserializationOption::NestingLimit(6)))return fail("character_project_json");
  const JsonObjectConst root=document.as<JsonObjectConst>();
  if(root.isNull()||root.size()!=7||!text_is(root["format"],"lunaredu.xiaoxing.character.v1"))return fail("character_project_format");
  const char *name=root["name"].as<const char *>();
  if(!name||!*name||std::strlen(name)>160||characters(name)>40)return fail("character_project_name");
  const char *expression=root["expression"].as<const char *>();
  const char *shape=root["shape"].as<const char *>();
  const char *color=root["color"].as<const char *>();
  if(!expression||!catalog::character_catalog_contains(catalog::kCharacterStates,expression)||
     !shape||!catalog::character_catalog_contains(catalog::kCharacterShapes,shape)||
     !color||!catalog::character_catalog_contains(catalog::kCharacterColors,color)||
     (!text_is(root["mode"],"locked")&&!text_is(root["mode"],"temporary")))return fail("character_project_appearance");
  if(!root["definitions"].is<JsonObjectConst>())return fail("character_project_definitions");
  const auto definitions=root["definitions"].as<JsonObjectConst>();
  if(definitions.size()>kExpressionCount)return fail("character_project_definitions");
  Project candidate;candidate.name=name;
  for(const auto item:definitions){
    const char *id=item.key().c_str();size_t slot=0;
    for(;slot<kExpressionCount;++slot)if(!std::strcmp(id,catalog::kCharacterStates[slot]))break;
    const JsonObjectConst d=item.value().as<JsonObjectConst>();
    if(slot==kExpressionCount||d.isNull()||d.size()!=8||!text_is(d["format"],"lunaredu.pet.expression.v1")||
       !text_is(d["id"],id)||!d["slot"].is<uint32_t>()||d["slot"].as<uint32_t>()!=slot)return fail("character_expression_identity");
    bool group=false;for(const char *g:{"Conversation","Task","System","Notice","Reaction","Cue","InteractionDemo"})group=group||text_is(d["group"],g);
    if(!group)return fail("character_expression_group");
    const JsonArrayConst eyes=d["eyePlaylist"].as<JsonArrayConst>();
    if(eyes.isNull()||eyes.size()<1||eyes.size()>6)return fail("character_expression_eyes");
    auto &config=candidate.expressions[slot];config.count=uint8_t(eyes.size());
    for(size_t i=0;i<eyes.size();++i){if(!eyes[i].is<uint32_t>()||eyes[i].as<uint32_t>()>=25)return fail("character_expression_eyes");config.eyes[i]=uint8_t(eyes[i].as<uint32_t>());}
    if(!range(d["holdMs"],config.hold,true)||!range(d["blinkMs"],config.blink,false))return fail("character_expression_timing");
    const JsonObjectConst motion=d["motion"].as<JsonObjectConst>();
    if(motion.isNull()||motion.size()!=2||!text_is(motion["kind"],"builtin")||!text_is(motion["preset"],id))return fail("character_expression_motion");
    candidate.overridden[slot]=true;
  }
  out=std::move(candidate);error.clear();return true;
}
}
