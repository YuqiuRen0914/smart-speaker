#pragma once
#include "desktop/composition/composition.h"
#include <tuple>
#include <utility>
#include <optional>
#include <type_traits>

namespace lunaredu::lunaros {
// Hooks run on the existing UI owner. Selection constructs retained sessions,
// but construction does not declare a foreground, prepare a VM or start a task.
struct DesktopSessionDefaults {
  void bind(ApplicationContext *) {}
  bool prepare(KernelRuntime &, std::string &) { return true; }
  bool recovery_required() const { return false; }
  template<class Ports> void application_ports(Ports &) {}
  bool declare(ApplicationContext &, std::string &) { return true; }
  void foreground_tick() {}
  void begin_tick() {}
  void poll(bool) {}
  bool widgets() const { return false; }
  std::optional<app::ViewRoute> destination() const { return {}; }
  bool shell_action(const char *) { return false; }
  void cancel_input() {}
  void release_transient_frames() {}
  void destination_committed(app::ViewRoute) {}
  bool launch_downloaded(const char *) { return false; }
  bool temporary_view(app::ViewRoute) const { return false; }
  void suspend_view() {}
  std::string running_application() const { return {}; }
  template<class Base> struct Context : Base {
    Context(Base base, DesktopSessionDefaults &) : Base(std::move(base)) {}
  };
};
namespace desktop_sessions_detail {
template<class Base, class... Sessions> struct ContextChain;
template<class Base> struct ContextChain<Base> {
  using type = Base;
  template<class Tuple> static Base make(Base base, Tuple &) { return base; }
};
template<class Base, class Session, class... Rest> struct ContextChain<Base, Session, Rest...> {
  using Layer = typename Session::template Context<Base>;
  using Tail = ContextChain<Layer, Rest...>;
  using type = typename Tail::type;
  template<class Tuple> static type make(Base base, Tuple &sessions) {
    return Tail::make(Layer{std::move(base), std::get<Session>(sessions)}, sessions);
  }
};
}
template<class... Sessions> class DesktopSessions {
 public:
  static constexpr size_t count = sizeof...(Sessions);
  template<class Host> explicit DesktopSessions(Host &host) : sessions_((static_cast<void>(sizeof(Sessions)), host)...) {}
  DesktopSessions(const DesktopSessions &) = delete;
  DesktopSessions &operator=(const DesktopSessions &) = delete;
  template<class Base> using Context = typename desktop_sessions_detail::ContextChain<Base, Sessions...>::type;
  template<class Base> Context<Base> context(Base base) {
    return desktop_sessions_detail::ContextChain<Base, Sessions...>::make(std::move(base), sessions_);
  }
  template<class Session, class Action> bool with(Action action) {
    if constexpr ((std::is_same_v<Session, Sessions> || ...)) { action(std::get<Session>(sessions_)); return true; }
    else { (void)action; return false; }
  }
  void bind(ApplicationContext *device) { each([&](auto &s) { s.bind(device); }); }
  bool prepare(KernelRuntime &runtime, std::string &error) { return all([&](auto &s) { return s.prepare(runtime, error); }); }
  bool recovery_required() const { return any([](const auto &s) { return s.recovery_required(); }); }
  template<class Ports> void application_ports(Ports &ports) { each([&](auto &s) { s.application_ports(ports); }); }
  bool declare(ApplicationContext &device, std::string &error) { return all([&](auto &s) { return s.declare(device, error); }); }
  void tick(bool allowed) {
    each([](auto &s) { s.foreground_tick(); });
    each([](auto &s) { s.begin_tick(); });
    each([&](auto &s) { s.poll(allowed); });
  }
  bool widgets() const { return any([](const auto &s) { return s.widgets(); }); }
  std::optional<app::ViewRoute> destination() const {
    std::optional<app::ViewRoute> result;
    std::apply([&](const auto &... s) { ([&] { if (!result) result = s.destination(); }(), ...); }, sessions_);
    return result;
  }
  bool shell_action(const char *id) { return any([&](auto &s) { return s.shell_action(id); }); }
  void cancel_input() { each([](auto &s) { s.cancel_input(); }); }
  void release_transient_frames() { each([](auto &s) { s.release_transient_frames(); }); }
  void destination_committed(app::ViewRoute page) { each([&](auto &s) { s.destination_committed(page); }); }
  bool launch_downloaded(const char *id) { return any([&](auto &s) { return s.launch_downloaded(id); }); }
  bool temporary_view(app::ViewRoute page) const { return any([&](const auto &s) { return s.temporary_view(page); }); }
  void suspend_view() { each([](auto &s) { s.suspend_view(); }); }
  std::string running_application() const {
    std::string result;
    std::apply([&](const auto &... s) { ([&] { if (result.empty()) result = s.running_application(); }(), ...); }, sessions_);
    return result;
  }
 private:
  template<class Action> void each(Action action) { std::apply([&](auto &... s) { (action(s), ...); }, sessions_); }
  template<class Action> bool all(Action action) { return std::apply([&](auto &... s) { return (action(s) && ...); }, sessions_); }
  template<class Action> bool any(Action action) { return std::apply([&](auto &... s) { return (action(s) || ...); }, sessions_); }
  template<class Action> bool any(Action action) const { return std::apply([&](const auto &... s) { return (action(s) || ...); }, sessions_); }
  std::tuple<Sessions...> sessions_;
};
}
