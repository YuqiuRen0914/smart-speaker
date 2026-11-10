#pragma once
namespace lunaredu::app {
// Immutable, host-neutral presentation snapshot tag. The host owns lifetime.
struct ApplicationSurfaceState {
  explicit ApplicationSurfaceState(const void *value):kind(value){}
  const void *kind;
};
}
