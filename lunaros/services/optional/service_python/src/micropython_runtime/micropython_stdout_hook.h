#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// MicroPython C 运行时写标准输出时调用的桥接函数。
// 这里保持 C ABI，方便第三方 C 源文件直接调用。
void micropython_runtime_append_stdout(const char *str, size_t len);

#ifdef __cplusplus
}
#endif
