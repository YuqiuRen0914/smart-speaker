#include "lunaros_ui_metrics.h"
#pragma once
#include "graphics/scene.h"
#include "lunaros_ui_theme.h"
#include "desktop/ui/content_layout.h"
#include "desktop/graphics/scene_icon.h"
#include <array>
#include <cstring>

namespace lunaredu::lunaros::graphics::page {
namespace ui=::lunaredu::lunaros::graphics::metrics;
namespace type=desktop::typography;
// 240x240 page content excludes the system header and the bottom Home strip.
inline constexpr Rect body{design::inset,design::contentTop,design::width,design::footerTop-design::contentTop-design::actionGap};
inline constexpr int footer_y=design::footerTop, footer_height=design::footerHeight;
inline constexpr uint32_t content_id=0xfff00000;
// Floating decoration in the parent's border box, including Flex row parents.
// A card that arranges this badge as content opts back in with flow_item().
bool icon_badge(Scene &scene,uint32_t id,Rect box,lunaros::icons::Icon icon,
                       uint16_t color=theme::appaccent,uint32_t parent=0,bool aa=true);
bool status_badge(Scene &scene,uint32_t id,Rect box,const char *text,uint16_t ink=theme::mint,uint32_t parent=0);
bool content_message(Scene &scene,const desktop::ContentLayout &layout,const char *headline,const char *detail,
                            theme::Tone tone=theme::Tone::Normal);
struct Action {
  uint32_t id=0;
  const char *label=nullptr;
  bool enabled=true;
  theme::Tone tone=theme::Tone::Accent;
};
bool actions(Scene &scene,Action secondary={},Action primary={});
bool content_actions(Scene &scene,const desktop::ContentLayout &layout,Action secondary={},Action primary={});
// Non-clickable labels sit over one actionable row; IDs stay separate from events.
// Simple rows use one scene slot, so the 33-entry startup menu fits the fixed pool.
bool row(Scene &scene,uint32_t id,Rect box,const char *title,const char *detail="",
                bool selected=false,bool enabled=true,uint32_t parent=0,
                theme::Tone tone=theme::Tone::Normal,int title_inset=12);
bool switch_row(Scene &scene,uint32_t id,uint32_t switch_id,Rect box,const char *title,
                       bool checked,bool selected=false,bool enabled=true,uint32_t parent=0,int title_inset=12);
bool information_row(Scene &scene,uint32_t id,Rect box,const char *title,const char *value);
bool stacked_action(Scene &scene,Action action,unsigned index,unsigned count,bool selected=false);
bool message(Scene &scene,const char *headline,const char *detail,
                    theme::Tone tone=theme::Tone::Normal);
enum class State { Loading, Empty, Error, Offline, ReadOnly };
// The caller chooses the state and actions; Desktop owns the card structure,
// text overflow and footer layout for either system-bar preference.
bool content_state(Scene &scene,const desktop::ContentLayout &,State,const char *detail,Action retry={});
bool state(Scene &scene,State state,const char *detail,Action retry={});
bool confirmation(Scene &scene,const char *headline,const char *consequence,
                         Action cancel,Action confirm);
bool result(Scene &scene,const char *headline,const char *detail,bool success,
                   Action done);
struct Choice {
  uint32_t id,detail_id;
  const char *label;
  bool selected=false,current=false;
};
// A short selection page. Longer collections use the OS scroll/list surface.
template<size_t count>
inline bool selection(Scene &scene,const std::array<Choice,count> &choices,Action back,Action save) {
  static_assert(count>0&&count<=3,"Short selection pages have one to three rows");
  constexpr auto group=content_id+1101;
  if(!scene.grid(group,{body.x,body.y,body.width,int(count)*ui::px(44)-ui::px(2)},3,count,0,ui::px(2)))return false;
  for(size_t i=0;i<choices.size();++i){
    const auto &c=choices[i];
    if(!scene.button(c.id,{0,0,body.width,ui::px(42)},c.label,type::label.size,theme::appsurface,
        theme::text,c.selected,true,group,Align::Left)||!scene.grid_item(c.id,0,i,3)||
       !scene.label(c.detail_id,{0,0,ui::px(56),ui::px(42)},c.current?"当前":"",type::secondary.size,
                    theme::appaccent,Align::Right,group)||!scene.grid_item(c.detail_id,2,i))return false;
  }
  return actions(scene,back,save);
}
struct Detail { const char *label,*value; };
template<size_t count>
inline bool information(Scene &scene,const std::array<Detail,count> &details,Action back) {
  static_assert(count<=4,"Information pages have at most four fields");
  if(count>3&&back.id)return false; // The fourth field occupies the footer space.
  constexpr auto group=content_id+1102;
  if constexpr(count>0)if(!scene.grid(group,{body.x,body.y,body.width,int(count)*ui::px(44)},2,count,ui::px(4),0))return false;
  for(size_t i=0;i<details.size();++i){
    const auto title=content_id+10+uint32_t(i)*2,value=title+1;
    if(!scene.label(title,{0,0,body.width/2,ui::px(44)},details[i].label,type::secondary.size,
                    theme::appsecondary,Align::Left,group)||!scene.grid_item(title,0,i)||
       !scene.label(value,{0,0,body.width/2,ui::px(44)},details[i].value,type::label.size,
                    theme::text,Align::Right,group)||!scene.grid_item(value,1,i))return false;
  }
  return actions(scene,back);
}
} // namespace lunaredu::lunaros::graphics::page
