#pragma once
#include <algorithm>
#include <cstdint>

namespace lunaredu::lunaros {
class BoundedWorkScope;
class BoundedWorkObserver {
 public:
  using Handler = void (*)(void *, bool, uint32_t);
  BoundedWorkObserver(Handler handler, void *context) : previous_(current_), handler_(handler), context_(context) { current_ = this; }
  ~BoundedWorkObserver() { current_ = previous_; }
  BoundedWorkObserver(const BoundedWorkObserver &) = delete;
  BoundedWorkObserver &operator=(const BoundedWorkObserver &) = delete;
 private:
  friend class BoundedWorkScope;
  inline static thread_local BoundedWorkObserver *current_ = nullptr;
  BoundedWorkObserver *previous_;
  Handler handler_;
  void *context_;
  bool active_ = false;
  uint32_t deadline_ = 0;
};
class BoundedWorkScope {
 public:
  BoundedWorkScope(uint32_t now, uint32_t duration) : observer_(BoundedWorkObserver::current_) {
    if (!observer_) return;
    previous_active_ = observer_->active_;
    previous_deadline_ = observer_->deadline_;
    auto deadline = now + std::min(duration, uint32_t(INT32_MAX));
    if (previous_active_ && int32_t(previous_deadline_ - deadline) < 0) deadline = previous_deadline_;
    observer_->active_ = true;
    observer_->deadline_ = deadline;
    observer_->handler_(observer_->context_, true, deadline);
  }
  ~BoundedWorkScope() {
    if (!observer_) return;
    observer_->active_ = previous_active_;
    observer_->deadline_ = previous_deadline_;
    observer_->handler_(observer_->context_, previous_active_, previous_deadline_);
  }
  BoundedWorkScope(const BoundedWorkScope &) = delete;
  BoundedWorkScope &operator=(const BoundedWorkScope &) = delete;
 private:
  BoundedWorkObserver *observer_;
  bool previous_active_ = false;
  uint32_t previous_deadline_ = 0;
};
}
