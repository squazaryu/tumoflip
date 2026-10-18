#pragma once
#include "test_stubs.h"
enum { FSAM_READ = 1, FSOM_OPEN_EXISTING = 1 };
bool file_stream_open(Stream* stream, const char* path, int access, int mode);
