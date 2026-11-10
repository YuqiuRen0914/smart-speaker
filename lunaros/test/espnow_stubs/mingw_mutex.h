#pragma once
#include <mutex>
#if defined(_WIN32) && !defined(_GLIBCXX_HAS_GTHREADS)
// The repository's MinGW Win32-thread compiler omits std::recursive_mutex.
// Tests use the native recursive primitive; firmware uses libstdc++/FreeRTOS.
#include <windows.h>
namespace std {
class recursive_mutex {
  CRITICAL_SECTION section_;
public:
  recursive_mutex() { InitializeCriticalSection(&section_); }
  ~recursive_mutex() { DeleteCriticalSection(&section_); }
  recursive_mutex(const recursive_mutex &) = delete;
  void lock() { EnterCriticalSection(&section_); }
  bool try_lock() { return TryEnterCriticalSection(&section_) != 0; }
  void unlock() { LeaveCriticalSection(&section_); }
};
}
#endif
