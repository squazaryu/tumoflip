#include "workspace_model.h"
#include <stddef.h>
#include <string.h>

bool workspace_name_valid(const char* name) {
    if(!name || !*name || *name == ' ') return false;
    size_t n = 0;
    for(; name[n]; n++) {
        const char c = name[n];
        if(n >= WORKSPACE_NAME_SIZE - 1 ||
           !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == ' ' || c == '-' || c == '_'))
            return false;
    }
    return name[n - 1] != ' ';
}

static bool workspace_frequency_valid(uint32_t f) {
    return (f >= 281000000 && f <= 361000000) || (f >= 378000000 && f <= 481000000) ||
           (f >= 749000000 && f <= 962000000);
}

static bool workspace_preset_valid(const char* name) {
    if(!name[0] || !memchr(name, 0, WORKSPACE_PRESET_SIZE)) return false;
    for(; *name; name++)
        if((unsigned char)*name < 32 || *name == ':') return false;
    return true;
}

bool workspace_profile_valid(const WorkspaceProfile* p, uint32_t pack_count) {
    return p && workspace_frequency_valid(p->frequency) &&
           workspace_frequency_valid(p->raw_frequency) && workspace_preset_valid(p->preset) &&
           workspace_preset_valid(p->raw_preset) && p->pack < pack_count && p->hopping < 4 &&
           p->radio < 3;
}
