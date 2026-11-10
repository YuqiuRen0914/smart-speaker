#pragma once
#include "desktop/graphics/canvas_spec.h"
#include "desktop/navigation/home_navigation.h"
namespace lunaredu::lunaros::graphics {class Scene;}

namespace lunaredu::desktop {
struct HomeTarget {float x=canvas_layout::center,y=134,diameter=60;};
struct HomeCardPose {float x,y,size,radius,opacity;};
HomeCardPose home_card_pose(const HomeNavigation &nav,HomeTarget target={});
bool compose_home_card(uint16_t *destination,const uint16_t *source,const HomeNavigation &nav,HomeTarget target={},unsigned rotation=0,bool antialias=true,lunaros::graphics::Scene *renderer=nullptr);
bool draw_home_indicator(const HomeNavigation &nav,uint16_t *pixels,unsigned rotation=0,bool antialias=true,lunaros::graphics::Scene *renderer=nullptr);
}
