#pragma once

#include <stdbool.h>
#include <stddef.h>

/** The RPC prefix is reserved; only the exact "RPC" token may be expanded internally. */
static inline bool rpc_app_args_forbid_context(const char* args) {
    return args && args[0] == 'R' && args[1] == 'P' && args[2] == 'C' &&
           args[3] != '\0';
}
