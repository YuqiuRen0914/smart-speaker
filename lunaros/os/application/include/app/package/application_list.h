#pragma once
#include "app/package/application_package.h"
#include <memory>
#include <initializer_list>
#include <utility>
#include <vector>

namespace lunaredu::lunaros::apps {
// Immutable metadata value. Copies retain the same array; publishing a new list
// cannot alter a frame which still holds the previous observation.
class ApplicationList {
public:
  ApplicationList() = default;
  explicit ApplicationList(std::vector<Application> entries)
      : entries_(std::make_shared<const std::vector<Application>>(std::move(entries))) {}
  ApplicationList(std::initializer_list<Application> entries)
      : ApplicationList(std::vector<Application>(entries)) {}
  const std::vector<Application> &entries() const {
    static const std::vector<Application> empty;
    return entries_ ? *entries_ : empty;
  }
  auto begin() const { return entries().begin(); }
  auto end() const { return entries().end(); }
  size_t size() const { return entries().size(); }
  bool empty() const { return entries().empty(); }
  const Application *data() const { return entries().data(); }
  const Application &operator[](size_t index) const { return entries()[index]; }
private:
  friend class ApplicationCatalogSnapshot;
  explicit ApplicationList(std::shared_ptr<const std::vector<Application>> entries)
      : entries_(std::move(entries)) {}
  std::shared_ptr<const std::vector<Application>> entries_;
};
}
