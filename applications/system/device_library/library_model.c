#include "library_model.h"
#include <string.h>

bool library_name_valid(const char* name) {
    if(!name || !*name || *name == ' ') return false;
    size_t n = 0;
    for(; name[n]; n++) {
        char c = name[n];
        if(n >= 24 || !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_'))
            return false;
    }
    return name[n - 1] != ' ';
}
bool library_path_valid(const char* path) {
    if(!path || strncmp(path, "/ext/", 5) || !path[5] || strlen(path) >= 256) return false;
    for(const char* p = path; *p; p++)
        if((unsigned char)*p < 32 || *p == '\\') return false;
    if(strstr(path, "//") || strstr(path, "/../") || strstr(path, "/./")) return false;
    const char* last = strrchr(path, '/');
    return last && last[1] && strcmp(last, "/..") && strcmp(last, "/.");
}
static bool library_text_valid(const char* text, size_t size) {
    if(!memchr(text, 0, size)) return false;
    for(; *text; text++)
        if((unsigned char)*text < 32) return false;
    return true;
}
bool library_card_valid(const DeviceCard* card) {
    if(!card || !library_text_valid(card->name, sizeof(card->name)) ||
       !library_name_valid(card->name) || !library_text_valid(card->notes, sizeof(card->notes)) ||
       !library_text_valid(card->tags, sizeof(card->tags)) ||
       !library_text_valid(card->checked_at, sizeof(card->checked_at)) ||
       card->link_count > LIBRARY_LINKS)
        return false;
    for(unsigned i = 0; i < card->link_count; i++) {
        if(!memchr(card->links[i], 0, sizeof(card->links[i])) ||
           !library_path_valid(card->links[i]))
            return false;
        for(unsigned j = 0; j < i; j++)
            if(!strcmp(card->links[i], card->links[j])) return false;
    }
    return true;
}
int library_newest_slot(const uint32_t gen[LIBRARY_SLOTS]) {
    int newest = -1;
    for(unsigned i = 0; i < LIBRARY_SLOTS; i++)
        if(gen[i] && (newest < 0 || gen[i] > gen[newest])) newest = i;
    return newest;
}
unsigned library_write_slot(const uint32_t gen[LIBRARY_SLOTS]) {
    unsigned slot = 0;
    for(unsigned i = 0; i < LIBRARY_SLOTS; i++)
        if(gen[i] < gen[slot]) slot = i;
    return slot;
}
