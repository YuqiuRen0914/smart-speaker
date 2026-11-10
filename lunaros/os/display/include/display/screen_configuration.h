#pragma once
#include "kernel/managed_task.h"
#include <cstddef>

namespace lunaredu::display {
struct ScreenMemory {
  void *context = nullptr;
  void *(*allocate)(void *, size_t) = nullptr;
  void (*free)(void *, void *) = nullptr;
};
ScreenMemory external_screen_memory();
// Owned storage plus explicit reservation for other screen consumers. SDK
// queues, stacks, shared LVGL and DMA must be reserved by their actual owner.
struct ScreenStorageBudget {
  size_t maximum_bytes = 2 * 1024 * 1024;
  size_t reserved_bytes = 0;
};
bool screen_storage_bytes(uint16_t width, uint16_t height, unsigned frames,
    size_t mailbox_bytes, ScreenStorageBudget budget, size_t &bytes, std::string &error);
struct ScreenPipelineTasks {
  lunaros::ManagedTaskDefinition display;
  lunaros::ManagedTaskDefinition renderer;
};
} // namespace lunaredu::display
