#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// VM-owned, read-only project sources. No host filesystem or transport access.
void micropython_project_init(void);
int micropython_project_stat(const char *path);
const char *micropython_project_source(const char *path, size_t *length);
#ifdef __cplusplus
}
#endif
