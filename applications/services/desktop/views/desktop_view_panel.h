#pragma once

#include <gui/canvas.h>
#include "../desktop_panel_i.h"

/* Renderer consumes one coherent cached model. It performs no I/O or allocation. */
void desktop_panel_draw(Canvas* canvas, void* model);
