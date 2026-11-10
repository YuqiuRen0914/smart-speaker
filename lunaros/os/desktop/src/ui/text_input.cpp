#include "desktop/ui/text_input.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mbedtls/platform_util.h>

namespace lunaredu::desktop {
namespace {
constexpr const char *groups[]={"abc","def","ghi","jkl","mno","pqrs","tuv","wxyz"};
constexpr char symbols[]=" !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
char letter(char c,bool upper) {return upper?static_cast<char>(c-'a'+'A'):c;}
}
void wipe_text(void *bytes,size_t length) {
  mbedtls_platform_zeroize(bytes,length);
}
TextInputState::~TextInputState(){wipe_text(value,sizeof(value));}
void TextInputState::clear(){wipe_text(value,sizeof(value));length=focus=symbol_page=0;group=-1;mode=TextInputMode::Letters;upper=revealed=last_visible=false;last_character_ms=0;}
size_t text_input_keys(const TextInputState &s,TextInputKeys &keys) {
  keys={};size_t count=0;
  const auto add=[&](TextInputAction action,const char *label,char value=0){
    auto &k=keys[count++];k.action=action;k.value=value;std::snprintf(k.label,sizeof(k.label),"%s",label);
  };
  const auto character=[&](char value){char label[2]={value,0};add(TextInputAction::Character,value==' '?"空格":label,value);};
  if(s.group>=0 && s.group<8){for(const char *p=groups[s.group];*p;++p)character(letter(*p,s.upper));}
  else if(s.mode==TextInputMode::Letters){
    for(int i=0;i<8;++i){char label[5]{};for(size_t n=0;n<std::strlen(groups[i]);++n)label[n]=letter(groups[i][n],s.upper);add(TextInputAction::Group,label,static_cast<char>(i));}
    add(TextInputAction::Done,"完成");
  }else {
    if(s.mode==TextInputMode::Numbers){for(char c: "1234567890")if(c)character(c);character(' ');}
    else {const size_t start=(s.symbol_page%3)*11;for(size_t i=start;i<std::min(start+11,sizeof(symbols)-1);++i)character(symbols[i]);}
    add(TextInputAction::Done,"完成");
  }
  if(s.group>=0)add(TextInputAction::Groups,"分组");
  else if(s.mode==TextInputMode::Letters)add(TextInputAction::Case,s.upper?"abc":"ABC");
  else add(TextInputAction::Letters,"abc");
  if(s.mode==TextInputMode::Symbols){char label[12];std::snprintf(label,sizeof(label),"%u/3 >",static_cast<unsigned>(s.symbol_page+1));add(TextInputAction::NextSymbols,label);}
  else add(s.mode==TextInputMode::Numbers?TextInputAction::Symbols:TextInputAction::Numbers,s.mode==TextInputMode::Numbers?"#+=":"123");
  add(TextInputAction::Delete,"删除");
  add(TextInputAction::Reveal,s.revealed?"隐藏":"显示");return count;
}
bool text_input_key(TextInputState &s,const TextInputKey &k,uint32_t now) {
  switch(k.action){
    case TextInputAction::Group:
      if(static_cast<unsigned char>(k.value)>=8)return false;
      s.group=k.value;s.focus=0;return true;
    case TextInputAction::Character:
      if(s.length>=64 || k.value<32 || k.value>126)return false;
      s.value[s.length++]=k.value;s.value[s.length]=0;s.last_visible=true;s.last_character_ms=now;
      if(s.group>=0){s.focus=static_cast<uint8_t>(s.group);s.group=-1;}return true;
    case TextInputAction::Delete:
      if(!s.length)return false;
      s.value[--s.length]=0;s.last_visible=false;return true;
    case TextInputAction::Reveal:s.revealed=!s.revealed;return true;
    case TextInputAction::Done:return false;
    case TextInputAction::Case:s.upper=!s.upper;s.mode=TextInputMode::Letters;break;
    case TextInputAction::Letters:case TextInputAction::Groups:s.mode=TextInputMode::Letters;break;
    case TextInputAction::Numbers:s.mode=TextInputMode::Numbers;break;
    case TextInputAction::Symbols:s.mode=TextInputMode::Symbols;s.symbol_page=0;break;
    case TextInputAction::NextSymbols:s.mode=TextInputMode::Symbols;s.symbol_page=(s.symbol_page+1)%3;break;
  }
  s.group=-1;s.focus=0;return true;
}
unsigned text_input_columns(const TextInputState &s){return s.group>=0?2:s.mode==TextInputMode::Letters?3:4;}
bool text_input_move(TextInputState &s,int direction,int step){
  TextInputKeys keys;const size_t count=text_input_keys(s,keys);if(!count)return false;
  size_t focus=std::min<size_t>(s.focus,count-1),next=focus;
  if(direction<0)next=(static_cast<int>(focus)+static_cast<int>(count)+(step<0?-1:1))%count;
  else {
    const unsigned columns=text_input_columns(s);float best=1e9f;
    for(size_t i=0;i<count;++i){
      const float dx=float(i%columns)-float(focus%columns),dy=float(i/columns)-float(focus/columns);
      const float along=direction==0?dx:direction==1?dy:direction==2?-dx:-dy;
      if(along<=.01f)continue;
      const float score=std::hypot(dx,dy)+4*std::abs(direction%2?dx:dy);
      if(score<best){best=score;next=i;}
    }
  }
  if(next==focus)return false;
  s.focus=static_cast<uint8_t>(next);return true;
}
bool text_input_back(TextInputState &s){
  if(s.group<0)return false;
  s.focus=static_cast<uint8_t>(s.group);s.group=-1;return true;
}
bool text_input_tick(TextInputState &s,uint32_t now){
  if(!s.last_visible||static_cast<uint32_t>(now-s.last_character_ms)<700)return false;
  s.last_visible=false;return true;
}
} // namespace lunaredu::desktop
