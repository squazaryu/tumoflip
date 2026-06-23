#pragma once
#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LoaderMenu LoaderMenu;

LoaderMenu* loader_menu_alloc(void (*closed_cb)(void*), void* context);
LoaderMenu* loader_menu_alloc_settings(void (*closed_cb)(void*), void* context);

void loader_menu_free(LoaderMenu* loader_menu);
bool loader_menu_has_pending_launch(LoaderMenu* loader_menu);
const char* loader_menu_get_pending_launch_name(LoaderMenu* loader_menu);
const char* loader_menu_get_pending_launch_args(LoaderMenu* loader_menu);

#ifdef __cplusplus
}
#endif
