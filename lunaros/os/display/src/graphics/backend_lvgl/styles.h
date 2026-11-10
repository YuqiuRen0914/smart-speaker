#pragma once
#include <lvgl/lvgl.h>
#include <initializer_list>
#include "lunaros_ui_theme.h"
namespace lunaredu::lunaros::graphics {
// Styles are shared only by widgets of one Scene. Init after lv_init;
// reset after every live and cached widget has released its references.
struct SceneStyles {
  lv_style_t card{},button_border{},track{},indicator{},knob{},textbox{},scrollbar{},keys{};
  bool ready=false;
  void init(lv_color_t accent,lv_color_t raised,lv_color_t text,lv_color_t secondary){
    if(ready)return;
    for(auto *s:{&card,&button_border,&track,&indicator,&knob,&textbox,&scrollbar,&keys})lv_style_init(s);
    lv_style_set_bg_opa(&card,LV_OPA_COVER);lv_style_set_radius(&card,12);lv_style_set_border_width(&card,0);
    lv_style_set_border_width(&button_border,1);lv_style_set_border_color(&button_border,accent);
    lv_style_set_bg_opa(&track,LV_OPA_COVER);lv_style_set_bg_color(&track,raised);
    lv_style_set_bg_opa(&indicator,LV_OPA_COVER);
    lv_style_set_bg_opa(&knob,LV_OPA_COVER);lv_style_set_bg_color(&knob,text);lv_style_set_radius(&knob,12);
    lv_style_set_bg_opa(&textbox,LV_OPA_COVER);lv_style_set_pad_all(&textbox,4);
    lv_style_set_bg_opa(&scrollbar,LV_OPA_COVER);lv_style_set_bg_color(&scrollbar,secondary);lv_style_set_width(&scrollbar,2);
    lv_style_set_bg_opa(&keys,LV_OPA_COVER);lv_style_set_radius(&keys,theme::radius);lv_style_set_text_color(&keys,text);
    ready=true;
  }
  void reset(){
    if(!ready)return;
    for(auto *s:{&card,&button_border,&track,&indicator,&knob,&textbox,&scrollbar,&keys})lv_style_reset(s);
    ready=false;
  }
};
}
