#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Private native-app route. Ordinary file/RPC launches keep their old meaning;
 * explicit wallet inspection accepts only a bounded, canonical SD path. */
static inline const char* saved_file_inspect_path(const char* argument, bool* inspect) {
    if(!argument || !inspect) return NULL;
    *inspect = strncmp(argument, "inspect:", 8) == 0;
    if(!*inspect) return argument;
    const char* path = argument + 8;
    if(strncmp(path, "/ext/", 5) || !path[5] || strlen(path) >= 256) return NULL;
    for(const char* p = path; *p; p++)
        if((unsigned char)*p < 32 || *p == '\\') return NULL;
    if(strstr(path, "//") || strstr(path, "/../") || strstr(path, "/./")) return NULL;
    const char* last = strrchr(path, '/');
    if(!last || !last[1] || !strcmp(last, "/..") || !strcmp(last, "/.")) return NULL;
    return path;
}
