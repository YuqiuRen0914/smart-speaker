#pragma once

#include <mutex>

#if defined(_WIN32) && !defined(_GLIBCXX_HAS_GTHREADS)
// Older MinGW Win32-thread builds omit std::mutex. Host tests use the native
// non-recursive lock; ESP-IDF continues to compile against its own std::mutex.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near
#undef far
namespace std {
class mutex {
public:
  mutex() = default;
  mutex(const mutex &) = delete;
  mutex &operator=(const mutex &) = delete;
  void lock() { AcquireSRWLockExclusive(&lock_); }
  bool try_lock() { return TryAcquireSRWLockExclusive(&lock_) != 0; }
  void unlock() { ReleaseSRWLockExclusive(&lock_); }

private:
  SRWLOCK lock_ = SRWLOCK_INIT;
};
} // namespace std
#endif
