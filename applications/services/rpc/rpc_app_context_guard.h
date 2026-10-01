#pragma once

#include <stdbool.h>
#include <stddef.h>

/** RPC context pointers are reserved for arguments generated inside RpcSystemApp. */
static inline bool rpc_app_args_forbid_context(const char* args) {
    return args && args[0] == 'R' && args[1] == 'P' && args[2] == 'C' &&
           (args[3] == ' ' || args[3] == '\t' || args[3] == '\n' || args[3] == '\r' ||
            args[3] == '\f' || args[3] == '\v');
}
