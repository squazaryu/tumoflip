#pragma once

#include "desktop.h"
#include "desktop_settings.h"

void desktop_panel_init(Desktop* desktop);
void desktop_panel_reload(Desktop* desktop);
bool desktop_panel_wanted(Desktop* desktop);
void desktop_panel_sync(Desktop* desktop);
void desktop_panel_pause(Desktop* desktop);
void desktop_panel_prepare_lock(Desktop* desktop);
void desktop_panel_after_app(Desktop* desktop);
void desktop_panel_move(Desktop* desktop, int8_t direction);
FavoriteApp* desktop_panel_favorite(Desktop* desktop, FavoriteAppShortcut shortcut);
