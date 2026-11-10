#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace lunaredu::app {
// Routes are declared by their owning App or OS surface. Local page numbers
// never become a product-wide enum or persisted routing contract.
constexpr uint32_t view_key(const char *owner, uint16_t local) {
  uint32_t value = 2166136261u;
  for (const char *p = owner; p && *p; ++p) value = (value ^ uint8_t(*p)) * 16777619u;
  return ((value ^ local) * 16777619u);
}
struct ViewRoute {
  const char *owner;
  uint16_t local;
  const char *name;
  bool widgets = false;
  bool fixed_orientation = false;
  uint32_t key;
  constexpr ViewRoute(const char *id, uint16_t page, const char *label,
      bool widget = false, bool fixed = false)
      : owner(id), local(page), name(label), widgets(widget), fixed_orientation(fixed), key(view_key(id, page)) {}
  constexpr bool operator==(const ViewRoute &other) const { return key == other.key; }
  constexpr bool operator!=(const ViewRoute &other) const { return !(*this == other); }
  constexpr explicit operator unsigned() const { return key; }
};
inline bool view_owned_by(ViewRoute route, const char *id) {
  return route.owner && id && !std::strcmp(route.owner, id);
}
template<class Context, size_t Capacity = 16> class ViewRegistry {
 public:
  using Paint = void (*)(Context &);
  bool add(const char *owner, Paint paint) {
    if (!owner || !*owner || !paint || size_ == Capacity) return false;
    for (size_t i=0;i<size_;++i) if (!std::strcmp(entries_[i].owner, owner)) return false;
    entries_[size_++]={owner,paint}; return true;
  }
  bool paint(ViewRoute route, Context &context) const {
    for (size_t i=0;i<size_;++i) if (view_owned_by(route, entries_[i].owner)) {
      entries_[i].paint(context); return true;
    }
    return false;
  }
 private:
  struct Entry { const char *owner; Paint paint; };
  Entry entries_[Capacity]{};
  size_t size_ = 0;
};
template<class Context, size_t Capacity = 16> class ViewInputRegistry {
 public:
  using Input = bool (*)(Context &);
  bool add(const char *owner,Input input) {
    if(!owner||!*owner||!input||size_==Capacity)return false;
    for(size_t i=0;i<size_;++i)if(!std::strcmp(entries_[i].owner,owner))return false;
    entries_[size_++]={owner,input};return true;
  }
  bool dispatch(ViewRoute route,Context &context) const {
    for(size_t i=0;i<size_;++i)if(view_owned_by(route,entries_[i].owner))return entries_[i].input(context);
    return false;
  }
 private:
  struct Entry { const char *owner; Input input; };
  Entry entries_[Capacity]{};
  size_t size_=0;
};
}
