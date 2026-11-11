#include "smart_speaker_launcher.h"

#include <array>
#include <initializer_list>

namespace smart_speaker {
namespace {
struct App {
  const char *title;
  uint32_t accent, surface;
};
constexpr std::array<App, 7> apps{{
    {"音乐", 0x66EDBB, 0x19352F},
    {"助手", 0xC8ACFF, 0x32254D},
    {"时间管理", 0xFBD47A, 0x3B3020},
    {"信息看板", 0x7ACDFF, 0x21324B},
    {"游戏娱乐", 0xFF9DB7, 0x412638},
    {"电脑协同", 0x91E5E9, 0x203A42},
    {"设置", 0xD1D9ED, 0x303644},
}};

lv_obj_t *centered_label(lv_obj_t *parent, const lv_font_t *font, int y, uint32_t color) {
  auto *label = lv_label_create(parent);
  lv_obj_set_width(label, 228);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  return label;
}
} // namespace

void AppLauncher::create(lv_obj_t *parent, const lv_font_t *heading, const lv_font_t *small) {
  root_ = lv_obj_create(parent);
  lv_obj_remove_style_all(root_);
  lv_obj_remove_flag(root_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_size(root_, 240, 240);
  lv_obj_set_style_bg_color(root_, lv_color_hex(0x11151E), 0);
  lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
  lv_label_set_text(centered_label(root_, small, 15, 0xA6B2C9), "应用");
  icon_ = lv_obj_create(root_);
  lv_obj_remove_style_all(icon_);
  lv_obj_remove_flag(icon_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_size(icon_, 120, 120);
  lv_obj_align(icon_, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_set_style_radius(icon_, 33, 0);
  lv_obj_set_style_bg_opa(icon_, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(icon_, draw_icon, LV_EVENT_DRAW_MAIN, this);
  title_ = centered_label(root_, heading, 177, 0xF3F6FF);
  for (unsigned i = 0; i < apps.size(); ++i) {
    dots_[i] = lv_obj_create(root_);
    lv_obj_remove_style_all(dots_[i]);
    lv_obj_remove_flag(dots_[i], static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(dots_[i], 5, 5);
    lv_obj_set_pos(dots_[i], 87 + i * 10, 218);
    lv_obj_set_style_radius(dots_[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dots_[i], LV_OPA_COVER, 0);
  }
  hide();
}

void AppLauncher::show(unsigned index) {
  if (index >= apps.size()) return;
  if (lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_move_foreground(root_);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
  }
  if (index_ == index) return;
  index_ = index;
  const auto &app = apps[index];
  lv_label_set_text(title_, app.title);
  lv_obj_set_style_bg_color(icon_, lv_color_hex(app.surface), 0);
  lv_obj_invalidate(icon_);
  for (unsigned i = 0; i < apps.size(); ++i)
    lv_obj_set_style_bg_color(dots_[i], lv_color_hex(i == index ? app.accent : 0x4A566D), 0);
}

void AppLauncher::hide() {
  if (!lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void AppLauncher::draw_icon(lv_event_t *event) {
  const auto index = static_cast<AppLauncher *>(lv_event_get_user_data(event))->index_;
  if (index >= apps.size()) return;
  lv_area_t box;
  lv_obj_get_coords(static_cast<lv_obj_t *>(lv_event_get_target(event)), &box);
  auto *layer = lv_event_get_layer(event);
  const auto color = lv_color_hex(apps[index].accent);
  const auto x = [&](int n) { return box.x1 + 23 + (n * 8 + 1) / 3; };
  const auto y = [&](int n) { return box.y1 + 23 + (n * 8 + 1) / 3; };
  const auto stroke = [&](std::initializer_list<lv_point_t> points) {
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color; line.width = 5; line.round_start = line.round_end = true;
    auto i = points.begin();
    line.p1 = {x(i->x), y(i->y)};
    for (++i; i != points.end(); ++i) {
      line.p2 = {x(i->x), y(i->y)};
      lv_draw_line(layer, &line);
      line.p1 = line.p2;
    }
  };
  const auto rect = [&](int l, int t, int r, int b, int radius, bool filled = false) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = (radius * 4 + 1) / 3; d.bg_color = d.border_color = color;
    d.bg_opa = filled ? LV_OPA_COVER : LV_OPA_TRANSP;
    d.border_width = filled ? 0 : 5;
    lv_area_t area{x(l), y(t), x(r), y(b)};
    lv_draw_rect(layer, &d, &area);
  };
  switch (index) {
    case 0:
      stroke({{10,20},{10,7},{23,4},{23,17}});
      stroke({{10,11},{23,8}});
      rect(3,18,11,24,8,true); rect(16,15,24,21,8,true);
      break;
    case 1:
      rect(2,5,23,20,12); stroke({{6,20},{6,25},{12,20}});
      stroke({{9,12},{9,14}}); stroke({{16,12},{16,14}});
      stroke({{24,2},{24,8}}); stroke({{21,5},{27,5}});
      break;
    case 2:
      rect(3,5,25,27,24); stroke({{10,1},{18,1}});
      stroke({{14,9},{14,16},{19,13}});
      break;
    case 3:
      rect(3,6,25,25,8); stroke({{3,12},{25,12}});
      stroke({{9,3},{9,9}}); stroke({{19,3},{19,9}});
      rect(8,16,11,19,4,true); rect(16,16,19,19,4,true);
      break;
    case 4:
      stroke({{8,8},{20,8},{24,11},{27,21},{25,24},{22,24},{18,20},{10,20},{6,24},{3,24},{1,21},{4,11},{8,8}});
      stroke({{8,11},{8,17}}); stroke({{5,14},{11,14}});
      rect(17,14,19,16,4,true); rect(21,11,23,13,4,true);
      break;
    case 5:
      rect(2,4,26,21,6); stroke({{9,25},{19,25}}); stroke({{14,21},{14,25}});
      stroke({{6,14},{10,14},{12,10},{15,17},{18,12},{22,12}});
      break;
    default:
      stroke({{11,2},{17,2},{18,6},{22,7},{25,10},{23,14},{25,18},
              {22,21},{18,22},{17,26},{11,26},{10,22},{6,21},{3,18},
              {5,14},{3,10},{6,7},{10,6},{11,2}});
      rect(10,10,18,18,10);
      break;
  }
}

} // namespace smart_speaker
