#include "desktop/graphics/page.h"

namespace lunaredu::lunaros::graphics::page {
bool icon_badge(Scene &scene,uint32_t id,Rect box,lunaros::icons::Icon icon,
                       uint16_t color,uint32_t parent,bool aa) {
  return scene.panel(id,box,theme::appraised,parent)&&scene.overlay(id,box)&&
    desktop::scene_icon(scene,id+1,{(box.width-ui::px(20))/2,(box.height-ui::px(20))/2,ui::px(20),ui::px(20)},icon,color,id,aa);
}

bool status_badge(Scene &scene,uint32_t id,Rect box,const char *text,uint16_t ink,uint32_t parent) {
  return scene.panel(id,box,theme::appraised,parent)&&
    scene.label(id+1,{ui::px(4),0,box.width-ui::px(8),box.height},text,ui::font(12),ink,Align::Center,id);
}

namespace {
bool message_card(Scene &scene,Rect box,const char *headline,const char *detail,theme::Tone tone){
  constexpr uint32_t card=content_id+101,header=content_id+106,text=content_id+104;
  const int inset=ui::px(12),width=box.width-inset*2,gap=ui::px(8),height=ui::px(36);
  return scene.panel(card,box,theme::appsurface)&&scene.flex_layout(card,false,gap,inset)&&
    scene.flow(header,{0,0,width,height},true,gap,card)&&
    icon_badge(scene,content_id+102,{0,0,height,height},tone==theme::Tone::Warning||tone==theme::Tone::Danger?lunaros::icons::Icon::Warning:lunaros::icons::Icon::Target,
      tone==theme::Tone::Normal?theme::appaccent:theme::ink(tone),header)&&scene.flow_item(content_id+102)&&
    scene.label(content_id+1,{0,0,width,type::title.line_height},headline,type::title.size,theme::ink(tone),Align::Left,card,true)&&
    scene.container(text,{0,0,width,ui::px(24)},0,0,false,card)&&scene.flow_item(text,1)&&
    scene.paragraph(content_id+2,{0,0,width,0},detail,ui::font(14),theme::appsecondary,text);
}
const char *state_title(State state){
  return state==State::Loading?"正在加载":state==State::Empty?"暂无内容":
    state==State::Error?"操作未完成":state==State::Offline?"暂不可用":"只读内容";
}
}
bool content_message(Scene &scene,const desktop::ContentLayout &layout,const char *headline,const char *detail,
                            theme::Tone tone) {
  return message_card(scene,{layout.inset,layout.top,layout.width,layout.height()-ui::px(48)},headline,detail,tone);
}

bool actions(Scene &scene,Action secondary,Action primary) {
  if(!secondary.id&&!primary.id)return true;
  constexpr uint32_t group=content_id+1100;
  if(!scene.flow(group,{design::inset,footer_y,design::width,footer_height},true,design::actionGap))return false;
  const auto draw=[&](Action a,bool leading){
    if(!a.id)return true;
    const bool filled=!leading;
    return scene.button(a.id,{0,0,design::width,footer_height},
        a.label,type::label.size,filled&&a.enabled?(a.tone==theme::Tone::Accent?theme::appaccent:theme::ink(a.tone)):theme::appsurface,
        !a.enabled?theme::appsecondary:filled?theme::background:theme::text,false,a.enabled,group)&&scene.flow_item(a.id,1,leading?0:secondary.id?1:0);
  };
  return draw(secondary,true)&&draw(primary,false);
}

bool content_actions(Scene &scene,const desktop::ContentLayout &layout,Action secondary,Action primary) {
  if(!secondary.id&&!primary.id)return true;
  constexpr uint32_t group=content_id+1100;
  if(!scene.flow(group,{layout.inset,layout.bottom-ui::px(40),layout.width,ui::px(40)},true,layout.gap))return false;
  const auto draw=[&](Action a,bool leading){
    if(!a.id)return true;
    const bool filled=!leading;
    return scene.button(a.id,{0,0,layout.width,ui::px(40)},
        a.label,type::label.size,filled&&a.enabled?(a.tone==theme::Tone::Accent?theme::appaccent:theme::ink(a.tone)):theme::appsurface,
        !a.enabled?theme::appsecondary:filled?theme::background:theme::text,false,a.enabled,group)&&scene.flow_item(a.id,1,leading?0:secondary.id?1:0);
  };
  return draw(secondary,true)&&draw(primary,false);
}

bool row(Scene &scene,uint32_t id,Rect box,const char *title,const char *detail,
                bool selected,bool enabled,uint32_t parent,
                theme::Tone tone,int title_inset) {
  if(!ui::native&&title_inset==12&&(!detail||!*detail))return scene.button(id,box,title,type::label.size,theme::appsurface,
      theme::ink(tone),selected,enabled,parent,Align::Left);
  const bool arrow=detail&&!std::strcmp(detail,">");
  const bool stacked=ui::native&&detail&&*detail&&!arrow;
  const int inset=ui::px(title_inset),trailing=ui::px(12),gap=ui::px(6);
  const auto title_id=0xffe00000+id*2,detail_id=title_id+1;
  if(!scene.button(id,box,"",type::label.size,theme::appsurface,theme::text,selected,enabled,parent)||
     !scene.flex_layout(id,!stacked,stacked?ui::gap/2:gap,inset,stacked?Align::Center:Align::Left,trailing)||
     !scene.label(title_id,{0,0,stacked?box.width-inset-trailing:0,type::label.line_height},title,type::label.size,
                  theme::ink(tone),Align::Left,id)||!scene.flow_item(title_id,stacked?0:1))return false;
  if(arrow)return desktop::scene_icon(scene,0xffd00000+id,{0,0,ui::px(14),ui::px(14)},
                                     lunaros::icons::Icon::ArrowRight,theme::appsecondary,id);
  if(!detail||!*detail)return true;
  return scene.label(detail_id,{0,0,stacked?box.width-inset-trailing:ui::px(84),type::secondary.line_height},detail,
                     type::secondary.size,theme::appsecondary,stacked?Align::Left:Align::Right,id);
}

bool switch_row(Scene &scene,uint32_t id,uint32_t switch_id,Rect box,const char *title,
                       bool checked,bool selected,bool enabled,uint32_t parent,int title_inset) {
  const auto label_id=0xffe00000+id*2;
  return scene.button(id,box,"",type::label.size,theme::appsurface,theme::text,selected,enabled,parent)&&
    scene.flex_layout(id,true,ui::px(8),ui::px(title_inset),Align::Left,ui::px(12))&&
    scene.label(label_id,{0,0,0,type::label.line_height},title,type::label.size,theme::text,Align::Left,id)&&
    scene.flow_item(label_id,1)&&scene.toggle(switch_id,{0,0,ui::px(44),ui::px(24)},checked,theme::appaccent,id);
}

bool information_row(Scene &scene,uint32_t id,Rect box,const char *title,const char *value) {
  const auto title_id=0xffe00000+id*2;
  return scene.panel(id,box,theme::appsurface)&&scene.flex_layout(id,true,ui::px(6),ui::px(12))&&
    scene.label(title_id,{0,0,0,type::secondary.line_height},title,type::secondary.size,theme::appsecondary,Align::Left,id)&&
    scene.flow_item(title_id,1)&&scene.label(title_id+1,{0,0,ui::px(84),type::label.line_height},value,type::label.size,theme::text,Align::Right,id);
}

bool stacked_action(Scene &scene,Action action,unsigned index,unsigned count,bool selected) {
  if(!action.id||!count||count>3||index>=count)return false;
  const int height=count==1?footer_height:ui::px(32),gap=count==1?0:ui::px(4);
  const int total=int(count)*height+int(count-1)*gap;
  constexpr auto group=content_id+1103;
  return scene.flow(group,{design::inset,design::contentBottom-total-gap,design::width,total},false,gap)&&
    scene.button(action.id,{0,0,design::width,height},action.label,type::label.size,theme::appsurface,
      action.enabled?theme::ink(action.tone):theme::appsecondary,selected,action.enabled,group)&&scene.flow_item(action.id,0,index);
}

bool message(Scene &scene,const char *headline,const char *detail,
                    theme::Tone tone) {
  return message_card(scene,body,headline,detail,tone);
}

bool content_state(Scene &scene,const desktop::ContentLayout &layout,State state,const char *detail,Action retry){
  return content_message(scene,layout,state_title(state),detail,state==State::Error?theme::Tone::Warning:theme::Tone::Normal)&&
    content_actions(scene,layout,{},retry);
}

bool state(Scene &scene,State state,const char *detail,Action retry) {
  return message(scene,state_title(state),detail,state==State::Error?theme::Tone::Warning:theme::Tone::Normal)&&actions(scene,{},retry);
}

bool confirmation(Scene &scene,const char *headline,const char *consequence,
                         Action cancel,Action confirm) {
  return message(scene,headline,consequence,confirm.tone)&&actions(scene,cancel,confirm);
}

bool result(Scene &scene,const char *headline,const char *detail,bool success,
                   Action done) {
  return message(scene,headline,detail,success?theme::Tone::Success:theme::Tone::Danger)&&
         actions(scene,{},done);
}
}
