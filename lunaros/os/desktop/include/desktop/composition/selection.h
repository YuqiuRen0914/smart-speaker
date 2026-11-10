#pragma once
#include "desktop/composition/composition.h"
#include "desktop/composition/sessions.h"
#include "desktop/application/downloaded_ui_snapshot.h"
#include "desktop/application/downloaded_host.h"
#include "desktop/application/foreground_route.h"
#include <tuple>
#include <variant>
#include <array>
#include <cstring>
#include <type_traits>

namespace lunaredu::lunaros {
// App-owned descriptions contain no runtime owner. Construction only assembles
// borrowed metadata; App factories and lifecycle callbacks run later.
struct DesktopEntryDefaults {
  using Presentation = void;
  static constexpr size_t menu_count = 0;
  static constexpr size_t menu_owner_count = 0;
  static constexpr size_t home_widget_count = 0;
  static const desktop::HomeWidgetDefinition *home_widgets() { return nullptr; }
  template<class Host> using Session = void;
  static const desktop::DesktopMenu *menus() { return nullptr; }
  static const desktop::DesktopMenuOwner *menu_owners() { return nullptr; }
  static DesktopAppDescriptor provider() { return {}; }
  template<class Ports> bool declare_native(app::AppController &, Ports &, std::string &) const { return true; }
};
template<class Entry> struct DesktopChoice {
  Entry entry;
  bool launcher, primary_menu;
};
template<class Entry> auto desktop_choice(Entry entry, bool launcher = true, bool primary_menu = false) {
  return DesktopChoice<Entry>{entry, launcher, primary_menu};
}
// Product options may specialize an App-owned entry, but cannot substitute an
// unrelated owner. Preserve derived types so their session/binding is retained.
template<class Expected, class Actual> auto checked_desktop_entry(Actual entry) {
  static_assert(std::is_base_of_v<Expected, Actual>, "Product options return the wrong desktop entry type");
  return entry;
}
namespace desktop_selection_detail {
template<class... Types> struct TypesList {};
template<class List, class Type> struct Append;
template<class... Types, class Type> struct Append<TypesList<Types...>, Type> {
  using type = std::conditional_t<std::is_void_v<Type> || (std::is_same_v<Type, Types> || ...),
      TypesList<Types...>, TypesList<Types..., Type>>;
};
template<class List, class... Entries> struct Presentations;
template<class List> struct Presentations<List> { using type = List; };
template<class List, class Entry, class... Rest> struct Presentations<List, Entry, Rest...>
    : Presentations<typename Append<List, typename Entry::Presentation>::type, Rest...> {};
template<class List> struct Variant;
template<class... Types> struct Variant<TypesList<Types...>> { using type = std::variant<Types...>; };
template<class List, class Host, class... Entries> struct Sessions;
template<class List, class Host> struct Sessions<List, Host> { using type = List; };
template<class List, class Host, class Entry, class... Rest> struct Sessions<List, Host, Entry, Rest...>
    : Sessions<typename Append<List, typename Entry::template Session<Host>>::type, Host, Rest...> {};
template<class List> struct SessionOwner;
template<class... Types> struct SessionOwner<TypesList<Types...>> { using type = DesktopSessions<Types...>; };
}
template<class... Entries> class DesktopSelection {
 public:
  template<class Host> using Sessions = typename desktop_selection_detail::SessionOwner<
      typename desktop_selection_detail::Sessions<desktop_selection_detail::TypesList<>, Host, Entries...>::type>::type;
  template<class Entry> static constexpr bool contains() { return (std::is_same_v<Entry, Entries> || ...); }
  using Presentation = typename desktop_selection_detail::Variant<
      typename desktop_selection_detail::Presentations<
          desktop_selection_detail::TypesList<desktop::ShellPresentation>, Entries...>::type>::type;
  explicit DesktopSelection(DesktopChoice<Entries>... choices) : choices_(choices...) {
    size_t index = 0, primary_count = 0;
    const auto append = [&](const auto &choice) {
      const auto tile = choice.entry.tile();
      if (!tile.id || !*tile.id || !tile.name || !*tile.name) valid_ = false;
      for (size_t i = 0; i < index; ++i)
        if (tile.id && catalog_.applications[i].id && !std::strcmp(tile.id, catalog_.applications[i].id)) valid_ = false;
      catalog_.applications[index++] = tile;
      if (choice.launcher) catalog_.launcher_storage[catalog_.launcher_count++] = tile;
      if(choice.entry.home_widget_count&&!choice.entry.home_widgets()){valid_=false;return;}
      for(size_t i=0;i<choice.entry.home_widget_count;++i){
        const auto &widget=choice.entry.home_widgets()[i];
        if(!widget.id||!*widget.id||std::strlen(widget.id)>63||!std::strncmp(widget.id,"system.",7)||
           !widget.application||!tile.id||std::strcmp(widget.application,tile.id)||!widget.title||!*widget.title||
           widget.width<1||widget.width>5||widget.height<1||widget.height>5||widget.alternate_height>5||widget.interval_ms<1000)valid_=false;
      }
      if (choice.entry.menu_owner_count && !choice.entry.menu_owners()) { valid_ = false; return; }
      for (size_t i = 0; i < choice.entry.menu_owner_count; ++i) {
        const auto owner = choice.entry.menu_owners()[i];
        if (!owner.menu_prefix || !*owner.menu_prefix || !owner.foreground_id ||
            !tile.id || std::strcmp(owner.foreground_id, tile.id)) valid_ = false;
        menu_owners_[menu_owner_count_++] = owner;
      }
      if (choice.entry.menu_count && !choice.entry.menus()) { valid_ = false; return; }
      if (choice.primary_menu && choice.entry.menu_count) {
        ++primary_count;
        catalog_.menu_storage[catalog_.menu_count++] = choice.entry.menus()[0];
      }
    };
    std::apply([&](const auto &... choices) { (append(choices), ...); }, choices_);
    const auto *declared_widgets=home_widgets();
    for(size_t i=0;i<home_widget_count;++i)for(size_t j=0;j<i;++j)
      if(declared_widgets[i].id&&declared_widgets[j].id&&!std::strcmp(declared_widgets[i].id,declared_widgets[j].id))valid_=false;
    if (primary_count > 1) valid_ = false;
    // Preserve primary root, other Apps' menus, then primary detail menus.
    const auto append_menus = [&](const auto &choice, bool primary) {
      if (choice.primary_menu != primary) return;
      if (!choice.entry.menus()) return;
      for (size_t i = primary ? 1 : 0; i < choice.entry.menu_count; ++i)
        catalog_.menu_storage[catalog_.menu_count++] = choice.entry.menus()[i];
    };
    std::apply([&](const auto &... choices) { (append_menus(choices, false), ...); }, choices_);
    std::apply([&](const auto &... choices) { (append_menus(choices, true), ...); }, choices_);
  }
  const auto &catalog() const { return catalog_; }
  auto menu_owners() const { return DesktopRange<desktop::DesktopMenuOwner>(menu_owners_.data(),menu_owner_count_); }
  bool valid() const { return valid_; }
  const DesktopAppDescriptor *providers() const { return provider_metadata().entries.data(); }
  size_t provider_count() const { return provider_metadata().count; }
  static constexpr size_t home_widget_count = (Entries::home_widget_count + ... + 0);
  const desktop::HomeWidgetDefinition *home_widgets() const {
    static const auto values=[] {
      std::array<desktop::HomeWidgetDefinition,home_widget_count> result{};size_t index=0;
      ([&]{for(size_t i=0;i<Entries::home_widget_count;++i){const auto *widgets=Entries::home_widgets();if(widgets)result[index]=widgets[i];++index;}}(),...);
      return result;
    }();
    return values.data();
  }
  template<class Bindings> Bindings bindings(const char *shell) const {
    Bindings result;
    if (!valid_ || !catalog_.contains(shell)) { (void)result.add({nullptr}); return result; }
    auto shell_binding = desktop::shell_ui_binding<Bindings>();
    if (!std::strcmp(shell, shell_binding.id))
      shell_binding.entered = desktop::entry_ui_binding<Bindings, desktop::shell_ui_entry>(shell).entered;
    (void)result.add(shell_binding);
    const auto add = [&](const auto &choice) {
      const auto binding = choice.entry.template binding<Bindings>();
      const auto tile = choice.entry.tile();
      const auto provider = choice.entry.provider();
      if (provider.id && (!provider.create || std::strcmp(provider.id, tile.id))) {
        (void)result.add({nullptr}); return;
      }
      if (!binding.id || std::strcmp(binding.id, tile.id)) { (void)result.add({nullptr}); return; }
      if (!std::strcmp(binding.id, shell_binding.id)) return;
      (void)result.add(binding);
    };
    std::apply([&](const auto &... choices) { (add(choices), ...); }, choices_);
    for (const auto &tile : catalog_.applications)
      if (!result.find(tile.id)) (void)result.add({nullptr});
    return result;
  }
  template<class Ports> bool declare_native(app::AppController &controller, Ports &ports, std::string &error) const {
    if (!valid_) { error = "invalid_desktop_selection"; return false; }
    bool ok = true;
    const auto declare = [&](const auto &choice) {
      if (ok) ok = choice.entry.declare_native(controller, ports, error);
    };
    std::apply([&](const auto &... choices) { (declare(choices), ...); }, choices_);
    return ok;
  }
 private:
  std::tuple<DesktopChoice<Entries>...> choices_;
  DesktopCatalog<sizeof...(Entries), (Entries::menu_count + ... + 0)> catalog_;
  std::array<desktop::DesktopMenuOwner, (Entries::menu_owner_count + ... + 0)> menu_owners_{};
  size_t menu_owner_count_ = 0;
  struct Providers {
    std::array<DesktopAppDescriptor, sizeof...(Entries)> entries{};
    size_t count = 0;
  };
  static const Providers &provider_metadata() {
    // Descriptions are App-owned static metadata. Reading them never invokes
    // factories, preparation, configuration or polling callbacks.
    static const Providers value = [] {
      Providers result;
      ([&] { const auto provider = Entries::provider();
        if (provider.id) result.entries[result.count++] = provider; }(), ...);
      return result;
    }();
    return value;
  }
  bool valid_ = true;
};
template<class... Entries> auto desktop_selection(DesktopChoice<Entries>... choices) {
  return DesktopSelection<Entries...>{choices...};
}
struct DesktopShellEntry : DesktopEntryDefaults {
  explicit DesktopShellEntry(desktop::DesktopApplication value) : identity(value) {}
  desktop::DesktopApplication identity;
  auto tile() const { return identity; }
  template<class Bindings> auto binding() const {
    return desktop::entry_ui_binding<Bindings, desktop::shell_ui_entry>(identity.id);
  }
};
struct DesktopDownloadedEntry : DesktopEntryDefaults {
  using Presentation = apps::DownloadedPresentation;
  static desktop::DesktopApplication tile() {
    return {apps::kDownloadedHostId, "下载应用运行器", desktop::DesktopIcon::Files, 0x45df};
  }
  template<class Bindings> static auto binding() { return apps::downloaded_ui_binding<Bindings>(); }
};
} // namespace lunaredu::lunaros
