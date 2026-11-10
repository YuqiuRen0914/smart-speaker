#pragma once

#include <type_traits>
extern "C" {
#include "py/nlr.h"
}

namespace lunaredu::micropython {
// Register immediately after constructing a native local that can survive a
// Python allocation or raise. MicroPython runs these callbacks before jumping;
// normal returns pop the callback and let C++ destroy the original local.
class NlrCleanup {
  struct Node {
    nlr_jump_callback_node_t callback;
    void *value;
    void (*destroy)(void *);
  } node_;

public:
  template <class T> explicit NlrCleanup(T &value)
      : node_{{}, const_cast<std::remove_const_t<T> *>(&value), [](void *p) {
          static_cast<std::remove_const_t<T> *>(p)->~T();
        }} {
    nlr_push_jump_callback(&node_.callback, [](void *p) {
      auto *node = static_cast<Node *>(p);
      node->destroy(node->value);
    });
  }
  ~NlrCleanup() { nlr_pop_jump_callback(false); }
  NlrCleanup(const NlrCleanup &) = delete;
  NlrCleanup &operator=(const NlrCleanup &) = delete;
};

// Materialize returned snapshots here instead of leaving unprotected temporary
// strings/vectors in callers while a converter allocates Python objects.
template <class T, class Convert> auto nlr_convert(T value, Convert convert) {
  NlrCleanup cleanup(value);
  return convert(value);
}
} // namespace lunaredu::micropython
