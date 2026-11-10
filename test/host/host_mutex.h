#pragma once

#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif
#include <mutex>

#if defined(_WIN32) && defined(__GLIBCXX__) && !defined(_GLIBCXX_HAS_GTHREADS)
// Older MinGW Win32-thread builds omit std::mutex. Host tests use the native
// non-recursive lock; ESP-IDF continues to compile against its own std::mutex.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near
#undef far
namespace std {

class recursive_mutex {
  CRITICAL_SECTION section_;
public:
  recursive_mutex() { InitializeCriticalSection(&section_); }
  ~recursive_mutex() { DeleteCriticalSection(&section_); }
  recursive_mutex(const recursive_mutex &) = delete;
  recursive_mutex &operator=(const recursive_mutex &) = delete;
  void lock() { EnterCriticalSection(&section_); }
  bool try_lock() { return TryEnterCriticalSection(&section_) != 0; }
  void unlock() { LeaveCriticalSection(&section_); }
};

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
