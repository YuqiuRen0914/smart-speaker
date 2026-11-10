#include "lunaros_ui_metrics.h"
#include "desktop/shell/notifications.h"
#include "graphics/scene.h"
#include "desktop/graphics/page.h"

namespace lunaredu::desktop::notifications {
namespace ui=::lunaredu::lunaros::graphics::metrics;
void build(lunaros::graphics::Scene &scene, const desktop::DesktopShell &shell,
           const desktop::DesktopStatus &) {
  using namespace lunaros::graphics;
  const desktop::ContentLayout layout(shell.preferences.auto_hide_status);
  const auto *view = state(shell);
  if (!view) {
    page::state(scene, page::State::Empty, "通知暂不可用");
    return;
  }
  if (!view->notices.count) {
    page::state(scene, page::State::Empty, "暂无通知");
    return;
  }
  constexpr size_t per_page = design::notificationsPerPage;
  const size_t first = (shell.row / per_page) * per_page;
  const int pitch=(layout.height()-ui::px(40))/int(per_page),height=pitch-ui::px(8);
  scene.flow(5100,{ui::px(8),layout.top,ui::px(224),layout.height()-ui::px(40)},false,ui::px(8));
  for (size_t i = first; i < std::min(first + per_page, view->notices.count); ++i) {
    const auto &entry = view->notices.entries[i];
    scene.button(1000 + i, {0,0,ui::px(224),height}, "",ui::font(14), theme::surface,
                 theme::text, shell.row == i, !entry.acknowledged,5100);
    scene.flow_item(1000+i,0,unsigned(i-first));
    scene.flex_layout(1000+i,false,ui::px(4),ui::px(4),Align::Center);
    scene.label(2000 + i, {0,0,ui::px(216),ui::px(18)}, entry.title.data(),ui::font(14),theme::text,Align::Left,1000+i,true);
    scene.label(3000 + i, {0,0,ui::px(216),ui::px(16)}, entry.kind == lunaros::notifications::Kind::Reminder ?
        (entry.acknowledged ? "已知晓 · 提醒仍有效" : "提醒 · 按确认知晓") :
        (entry.acknowledged ? "消息 · 已读" : "消息 · 按确认已读"),ui::font(12),theme::secondary,Align::Left,1000+i,true);
  }
  page::content_actions(scene,layout, {5008, "上一条"}, {5009, "下一条"});
}
bool action(desktop::DesktopShell &shell, lunaros::graphics::Action value) {
  const auto *view = state(shell);
  if (!view) return false;
  if (value.id == 5008 || value.id == 5009)
    return shell.command(value.id == 5008 ? desktop::DesktopCommand::Previous :
                         desktop::DesktopCommand::Next);
  if (value.id >= 1000 && value.id < 1000 + view->notices.count) {
    shell.row = uint8_t(value.id - 1000);
    return shell.command(desktop::DesktopCommand::Confirm);
  }
  return false;
}
} // namespace lunaredu::desktop::notifications
