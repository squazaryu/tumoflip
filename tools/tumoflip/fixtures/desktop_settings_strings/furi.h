#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define furi_assert(condition) assert(condition)
#define furi_check(condition) assert(condition)
#define FURI_LOG_I(...) ((void)0)
#define FURI_LOG_W(...) ((void)0)
#define FURI_LOG_E(...) ((void)0)

void* furi_record_open(const char* name);
void furi_record_close(const char* name);
