#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LUNAROS_FILE_FOLDER,
    LUNAROS_FILE_PYTHON,
    LUNAROS_FILE_PERFORMANCE,
    LUNAROS_FILE_OTHER,
} lunaros_file_kind_t;

typedef enum {
    LUNAROS_SERVER_DISCONNECTED,
    LUNAROS_SERVER_CONNECTED,
    LUNAROS_SERVER_ERROR,
} lunaros_server_connection_state_t;

typedef enum {
    LUNAROS_HOST_DISCONNECTED,
    LUNAROS_HOST_CONNECTED,
    LUNAROS_HOST_ERROR,
    LUNAROS_HOST_LAN,
} lunaros_host_connection_state_t;

#ifdef __cplusplus
}
#endif
