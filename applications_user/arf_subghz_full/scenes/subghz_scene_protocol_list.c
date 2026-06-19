#include "../subghz_i.h"
#include <lib/subghz/subghz_protocol_registry.h>

#define PROTOCOL_LIST_PAGE_SIZE 10
#define PROTOCOL_EVENT_BASE     1000
#define PROTOCOL_EVENT_PREV     1
#define PROTOCOL_EVENT_NEXT     2
#define PROTOCOL_EVENT_CLEAR    3

static size_t protocol_list_page = 0;

static bool proto_filter_contains(const char* filter, const char* name) {
    const char* p = filter;
    while(*p) {
        const char* comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if(len == strlen(name) && strncmp(p, name, len) == 0) return true;
        if(!comma) break;
        p = comma + 1;
    }
    return false;
}

static void proto_filter_append_segment(char* dst, size_t dst_size, const char* src, size_t src_len) {
    size_t used = strlen(dst);
    if(used >= dst_size - 1) return;

    size_t available = dst_size - used - 1;
    size_t copy_len = src_len < available ? src_len : available;
    memcpy(dst + used, src, copy_len);
    dst[used + copy_len] = '\0';
}

static void proto_filter_toggle(char* filter, size_t filter_size, const char* name) {
    if(proto_filter_contains(filter, name)) {
        char tmp[256] = {0};
        const char* p = filter;
        bool first = true;
        while(*p) {
            const char* comma = strchr(p, ',');
            size_t len = comma ? (size_t)(comma - p) : strlen(p);
            if(!(len == strlen(name) && strncmp(p, name, len) == 0)) {
                if(!first) strlcat(tmp, ",", sizeof(tmp));
                proto_filter_append_segment(tmp, sizeof(tmp), p, len);
                first = false;
            }
            if(!comma) break;
            p = comma + 1;
        }
        strlcpy(filter, tmp, filter_size);
    } else {
        if(filter[0] != '\0') strlcat(filter, ",", filter_size);
        strlcat(filter, name, filter_size);
    }
}

static void subghz_scene_protocol_list_callback(void* context, uint32_t index) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, index);
}

static void subghz_scene_protocol_list_rebuild(SubGhz* subghz) {
    Submenu* submenu = subghz->submenu;
    submenu_reset(submenu);

    const size_t protocol_count = subghz_protocol_registry_count(&subghz_protocol_registry);
    const size_t page_count =
        protocol_count ? ((protocol_count + PROTOCOL_LIST_PAGE_SIZE - 1) / PROTOCOL_LIST_PAGE_SIZE) : 1;
    if(protocol_list_page >= page_count) protocol_list_page = page_count - 1;

    char header[32];
    snprintf(header, sizeof(header), "Protocols %u/%u", (unsigned)(protocol_list_page + 1), (unsigned)page_count);
    submenu_set_header(submenu, header);

    if(subghz->last_settings->protocol_filter[0] != '\0') {
        submenu_add_item(
            submenu,
            "[clear filter]",
            PROTOCOL_EVENT_CLEAR,
            subghz_scene_protocol_list_callback,
            subghz);
    }

    const size_t start = protocol_list_page * PROTOCOL_LIST_PAGE_SIZE;
    const size_t end = MIN(start + PROTOCOL_LIST_PAGE_SIZE, protocol_count);

    for(size_t i = start; i < end; i++) {
        const SubGhzProtocol* protocol =
            subghz_protocol_registry_get_by_index(&subghz_protocol_registry, i);
        if(!protocol) continue;

        char label[64];
        snprintf(
            label,
            sizeof(label),
            "%c %.56s",
            proto_filter_contains(subghz->last_settings->protocol_filter, protocol->name) ? '*' : '-',
            protocol->name);

        submenu_add_item(
            submenu,
            label,
            PROTOCOL_EVENT_BASE + i,
            subghz_scene_protocol_list_callback,
            subghz);
    }

    if(protocol_list_page > 0) {
        submenu_add_item(
            submenu, "< Prev", PROTOCOL_EVENT_PREV, subghz_scene_protocol_list_callback, subghz);
    }
    if(protocol_list_page + 1 < page_count) {
        submenu_add_item(
            submenu, "Next >", PROTOCOL_EVENT_NEXT, subghz_scene_protocol_list_callback, subghz);
    }

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdMenu);
}

void subghz_scene_protocol_list_on_enter(void* context) {
    SubGhz* subghz = context;
    protocol_list_page = scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneProtocolList);
    subghz_scene_protocol_list_rebuild(subghz);
}

bool subghz_scene_protocol_list_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_set_scene_state(
            subghz->scene_manager, SubGhzSceneProtocolList, protocol_list_page);
        scene_manager_previous_scene(subghz->scene_manager);
        return true;
    }

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == PROTOCOL_EVENT_PREV) {
        if(protocol_list_page > 0) protocol_list_page--;
        subghz_scene_protocol_list_rebuild(subghz);
        return true;
    }

    if(event.event == PROTOCOL_EVENT_NEXT) {
        protocol_list_page++;
        subghz_scene_protocol_list_rebuild(subghz);
        return true;
    }

    if(event.event == PROTOCOL_EVENT_CLEAR) {
        subghz->last_settings->protocol_filter[0] = '\0';
        subghz_last_settings_save(subghz->last_settings);
        subghz_scene_protocol_list_rebuild(subghz);
        return true;
    }

    if(event.event >= PROTOCOL_EVENT_BASE) {
        const size_t protocol_index = event.event - PROTOCOL_EVENT_BASE;
        const SubGhzProtocol* protocol =
            subghz_protocol_registry_get_by_index(&subghz_protocol_registry, protocol_index);
        if(protocol) {
            proto_filter_toggle(
                subghz->last_settings->protocol_filter,
                sizeof(subghz->last_settings->protocol_filter),
                protocol->name);
            subghz_last_settings_save(subghz->last_settings);
            subghz_scene_protocol_list_rebuild(subghz);
        }
        return true;
    }

    return false;
}

void subghz_scene_protocol_list_on_exit(void* context) {
    SubGhz* subghz = context;
    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneProtocolList, protocol_list_page);
    submenu_reset(subghz->submenu);
}
