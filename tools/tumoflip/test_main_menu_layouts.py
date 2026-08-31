#!/usr/bin/env python3
"""Regression contracts for the 128x64 desktop main-menu layouts.

The firmware GUI is not linked into the host tests, so this suite deliberately
combines two layers:

* source contracts for persistence, bounded text, and allocation-free draws;
* a host-compiled transition matrix that executes the pure navigation helper
  extracted from ``menu.c`` itself.

The checks avoid golden pixels and individual decoration coordinates.  They
protect the layout/navigation boundaries while leaving visual polish free to
evolve.
"""

from __future__ import annotations

from collections import deque
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
MENU_SOURCE = REPO_ROOT / "applications/services/gui/modules/menu.c"
MENU_STYLE_HEADER = REPO_ROOT / "applications/services/gui/modules/menu_style.h"
MENU_STYLE_SOURCE = REPO_ROOT / "applications/services/gui/modules/menu_style.c"

LEGACY_STYLE_IDS = {
    "MenuStyleList": 0,
    "MenuStyleWii": 1,
    "MenuStyleDsi": 2,
    "MenuStyleVertical": 3,
    "MenuStyleWiiVertical": 4,
}

VISIBLE_STYLE_NAMES = (
    "List",
    "Matrix",
    "Rail",
    "Side List",
    "Side Grid",
    "Wii",
)

EXPECTED_DRAW_FUNCTIONS = (
    "menu_draw_list",
    "menu_draw_signal_matrix",
    "menu_draw_signal_rail",
    "menu_draw_vertical",
    "menu_draw_wii_vertical",
    "menu_draw_wii_classic",
)


def source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _matching_brace(contents: str, opening: int) -> int:
    """Return the closing brace while ignoring braces in comments/strings."""

    depth = 0
    index = opening
    state = "code"
    while index < len(contents):
        char = contents[index]
        following = contents[index + 1] if index + 1 < len(contents) else ""

        if state == "code":
            if char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "/" and following == "/":
                state = "line_comment"
                index += 1
            elif char == "/" and following == "*":
                state = "block_comment"
                index += 1
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return index
        elif state in ("string", "char"):
            if char == "\\":
                index += 1
            elif (state == "string" and char == '"') or (
                state == "char" and char == "'"
            ):
                state = "code"
        elif state == "line_comment":
            if char == "\n":
                state = "code"
        elif state == "block_comment" and char == "*" and following == "/":
            state = "code"
            index += 1

        index += 1

    raise AssertionError("unbalanced C function body")


def function_definition(contents: str, name: str) -> str:
    """Extract one ordinary C function definition by name."""

    match = re.search(
        rf"(?m)^(?:static\s+)?[\w\s\*]+\b{re.escape(name)}\s*\([^;]*?\)\s*{{",
        contents,
        flags=re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"function not found: {name}")
    opening = contents.find("{", match.start(), match.end())
    closing = _matching_brace(contents, opening)
    return contents[match.start() : closing + 1]


def local_function_definitions(contents: str) -> dict[str, str]:
    """Return definitions for local functions used to inspect the draw graph."""

    names = re.findall(
        r"(?m)^(?:static\s+)?[\w\s\*]+\b(menu_[a-zA-Z0-9_]+)\s*\([^;]*?\)\s*{",
        contents,
        flags=re.DOTALL,
    )
    return {name: function_definition(contents, name) for name in names}


def reachable_functions(definitions: dict[str, str], entry: str) -> dict[str, str]:
    """Follow only calls to functions defined in the same source file."""

    reached: dict[str, str] = {}
    pending = deque([entry])
    while pending:
        name = pending.popleft()
        if name in reached:
            continue
        body = definitions[name]
        reached[name] = body
        for candidate in definitions:
            if candidate not in reached and re.search(
                rf"\b{re.escape(candidate)}\s*\(", body
            ):
                pending.append(candidate)
    return reached


def parse_enum_values(header: str) -> dict[str, int]:
    match = re.search(r"typedef\s+enum\s*{(?P<body>.*?)}\s*MenuStyle\s*;", header, re.DOTALL)
    if match is None:
        raise AssertionError("MenuStyle enum not found")

    values: dict[str, int] = {}
    next_value = 0
    for raw_entry in match.group("body").split(","):
        entry = re.sub(r"/\*.*?\*/|//.*", "", raw_entry, flags=re.DOTALL).strip()
        if not entry:
            continue
        if "=" in entry:
            name, raw_value = (part.strip() for part in entry.split("=", 1))
            next_value = int(raw_value, 0)
        else:
            name = entry
        values[name] = next_value
        next_value += 1
    return values


def parse_name_table(contents: str) -> tuple[str, ...]:
    match = re.search(
        r"names\s*\[\s*MenuStyleCount\s*]\s*=\s*{(?P<body>.*?)}\s*;",
        contents,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError("menu style name table not found")
    return tuple(re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', match.group("body")))


def numeric_define(contents: str, name: str) -> int:
    match = re.search(
        rf"(?m)^\s*#define\s+{re.escape(name)}\s+\(?\s*(0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*\s*\)?\s*$",
        contents,
    )
    if match is None:
        raise AssertionError(f"numeric define not found: {name}")
    return int(match.group(1), 0)


class MainMenuLayoutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.menu = source(MENU_SOURCE)
        cls.style_header = source(MENU_STYLE_HEADER)
        cls.style_source = source(MENU_STYLE_SOURCE)
        cls.functions = local_function_definitions(cls.menu)

    def test_legacy_style_ids_and_one_byte_storage_remain_compatible(self) -> None:
        enum_values = parse_enum_values(self.style_header)
        for name, expected_id in LEGACY_STYLE_IDS.items():
            self.assertEqual(enum_values.get(name), expected_id)
        self.assertEqual(enum_values.get("MenuStyleWiiClassic"), 5)
        self.assertEqual(enum_values.get("MenuStyleCount"), 6)
        for contents in (self.style_header, self.style_source):
            self.assertIn("uint8_t style;", contents)
            self.assertIn("settings.style < MenuStyleCount", contents)
            self.assertIn("MENU_STYLE_SETTINGS_MAGIC (0x87)", contents)
            self.assertIn("MENU_STYLE_SETTINGS_VER   (3)", contents)

    def test_visible_names_match_in_static_and_linked_implementations(self) -> None:
        header_names = parse_name_table(self.style_header)
        source_names = parse_name_table(self.style_source)
        style_count = parse_enum_values(self.style_header)["MenuStyleCount"]
        self.assertEqual(header_names, source_names)
        self.assertEqual(header_names[: len(VISIBLE_STYLE_NAMES)], VISIBLE_STYLE_NAMES)
        self.assertEqual(len(header_names), style_count)

    def test_draw_graph_does_not_allocate_or_free_heap_memory(self) -> None:
        graph = reachable_functions(self.functions, "menu_draw_callback")
        draw_path = "\n".join(graph.values())
        forbidden = (
            "furi_string_alloc",
            "furi_string_free",
            "malloc(",
            "calloc(",
            "realloc(",
            "free(",
        )
        for operation in forbidden:
            self.assertNotIn(operation, draw_path, f"heap operation in draw path: {operation}")

    def test_label_scratch_is_owned_by_the_menu_lifecycle(self) -> None:
        allocation = function_definition(self.menu, "menu_alloc")
        release = function_definition(self.menu, "menu_free")

        self.assertIn("FuriString* label_scratch;", self.menu)
        self.assertIn("model->label_scratch = furi_string_alloc();", allocation)
        self.assertIn(
            "furi_string_reserve(model->label_scratch, MENU_LABEL_SCRATCH_CAPACITY);",
            allocation,
        )
        self.assertIn("furi_string_free(model->label_scratch);", release)
        self.assertIn("model->label_scratch = NULL;", release)

    def test_label_scratch_capacity_never_shrinks_with_item_order(self) -> None:
        """A short item added after a long one must not undo draw-path capacity."""

        allocation = function_definition(self.menu, "menu_alloc")
        add_item = function_definition(self.menu, "menu_add_item")
        reset = function_definition(self.menu, "menu_reset")
        release = function_definition(self.menu, "menu_free")

        self.assertIn("size_t label_scratch_capacity;", self.menu)
        self.assertIn(
            "model->label_scratch_capacity = MENU_LABEL_SCRATCH_CAPACITY;",
            allocation,
        )
        self.assertRegex(
            add_item,
            r"if\s*\(label_capacity\s*>\s*model->label_scratch_capacity\)\s*{"
            r"[^}]*furi_string_reserve\(model->label_scratch,\s*label_capacity\);"
            r"[^}]*model->label_scratch_capacity\s*=\s*label_capacity;",
        )
        self.assertRegex(
            add_item,
            r"if\s*\(label_capacity\s*<\s*MENU_LABEL_SCRATCH_CAPACITY\)\s*{"
            r"[^}]*label_capacity\s*=\s*MENU_LABEL_SCRATCH_CAPACITY;",
        )
        self.assertNotRegex(
            add_item,
            r"furi_string_reserve\(model->label_scratch,\s*strlen\(label\)",
        )
        self.assertIn("furi_string_reset(model->label_scratch);", reset)
        self.assertNotIn("furi_string_reserve(", reset)
        self.assertIn("model->label_scratch_capacity = 0;", reset)
        self.assertIn("model->label_scratch_capacity = 0;", release)

    def test_every_layout_routes_item_labels_through_a_bounded_helper(self) -> None:
        fitters = {
            name
            for name, definition in self.functions.items()
            if "elements_string_fit_width(" in definition
        }
        self.assertTrue(fitters, "no bounded label helper found")

        callback = function_definition(self.menu, "menu_draw_callback")
        renderers = set(re.findall(r"\b(menu_draw_[a-zA-Z0-9_]+)\s*\(", callback))
        renderers.discard("menu_draw_callback")
        self.assertTrue(set(EXPECTED_DRAW_FUNCTIONS).issubset(renderers))

        for renderer in sorted(renderers):
            graph = reachable_functions(self.functions, renderer)
            self.assertTrue(
                fitters.intersection(graph),
                f"{renderer} does not reach a bounded label helper",
            )
            direct_draws = re.findall(
                r"canvas_draw_str(?:_aligned)?\s*\([^;]+;",
                graph[renderer],
                flags=re.DOTALL,
            )
            for draw in direct_draws:
                self.assertNotRegex(
                    draw,
                    r"(?:item->label|menu_short_name\s*\(\s*item->label\s*\))",
                    f"{renderer} draws an unbounded item label",
                )

    def test_named_layout_geometry_fits_the_128_by_64_framebuffer(self) -> None:
        value = lambda name: numeric_define(self.menu, name)
        screen_width = value("MENU_SCREEN_WIDTH")
        screen_height = value("MENU_SCREEN_HEIGHT")
        self.assertEqual((screen_width, screen_height), (128, 64))

        header_baseline = value("MENU_HEADER_BASELINE")
        header_divider = value("MENU_HEADER_DIVIDER_Y")
        header_x = value("MENU_HEADER_LABEL_X")
        header_width = value("MENU_HEADER_LABEL_MAX_WIDTH")
        counter_x = value("MENU_HEADER_COUNTER_X")
        self.assertLess(header_baseline, screen_height)
        self.assertLess(header_divider, screen_height)
        self.assertLessEqual(header_x + header_width, screen_width)
        self.assertLess(counter_x, screen_width)

        page_size = value("MENU_MATRIX_PAGE_SIZE")
        columns = value("MENU_MATRIX_COLUMNS")
        rows = value("MENU_MATRIX_ROWS")
        cell_width = value("MENU_MATRIX_CELL_WIDTH")
        cell_height = value("MENU_MATRIX_CELL_HEIGHT")
        first_center_x = value("MENU_MATRIX_FIRST_CENTER_X")
        first_center_y = value("MENU_MATRIX_FIRST_CENTER_Y")
        selected_width = value("MENU_MATRIX_SELECTED_WIDTH")
        selected_height = value("MENU_MATRIX_SELECTED_HEIGHT")
        self.assertEqual(page_size, columns * rows)
        last_center_x = first_center_x + (columns - 1) * cell_width
        last_center_y = first_center_y + (rows - 1) * cell_height
        self.assertLess(header_divider, first_center_y - selected_height // 2)
        self.assertGreaterEqual(first_center_x, selected_width // 2)
        self.assertLessEqual(last_center_x + selected_width // 2, screen_width)
        self.assertGreaterEqual(first_center_y, selected_height // 2)
        self.assertLessEqual(last_center_y + selected_height // 2, screen_height)
        self.assertLessEqual(selected_width, cell_width)
        self.assertLessEqual(selected_height, cell_height)

        wii_page_size = value("MENU_WII_PAGE_SIZE")
        wii_columns = value("MENU_WII_COLUMNS")
        wii_rows = value("MENU_WII_ROWS")
        wii_column_step = value("MENU_WII_COLUMN_STEP")
        wii_row_step = value("MENU_WII_ROW_STEP")
        wii_first_x = value("MENU_WII_FIRST_X")
        wii_first_y = value("MENU_WII_FIRST_Y")
        wii_cell_width = value("MENU_WII_CELL_WIDTH")
        wii_cell_height = value("MENU_WII_CELL_HEIGHT")
        wii_icon_height = value("MENU_WII_ICON_HEIGHT")
        wii_label_baseline = value("MENU_WII_LABEL_BASELINE")
        wii_label_width = value("MENU_WII_LABEL_MAX_WIDTH")
        self.assertEqual(wii_page_size, wii_columns * wii_rows)
        self.assertLessEqual(
            wii_first_x + (wii_columns - 1) * wii_column_step + wii_cell_width,
            screen_width,
        )
        self.assertLessEqual(
            wii_first_y + (wii_rows - 1) * wii_row_step + wii_cell_height,
            screen_height,
        )
        self.assertLessEqual(wii_icon_height, wii_cell_height)
        self.assertLess(wii_label_baseline, wii_cell_height)
        self.assertLessEqual(wii_label_width, wii_cell_width)

        previous_x = value("MENU_RAIL_PREVIOUS_X")
        selected_x = value("MENU_RAIL_SELECTED_X")
        next_x = value("MENU_RAIL_NEXT_X")
        center_y = value("MENU_RAIL_ICON_CENTER_Y")
        side_width = value("MENU_RAIL_SIDE_WIDTH")
        side_height = value("MENU_RAIL_SIDE_HEIGHT")
        selected_width = value("MENU_RAIL_SELECTED_WIDTH")
        selected_height = value("MENU_RAIL_SELECTED_HEIGHT")
        connector_x = value("MENU_RAIL_CONNECTOR_X")
        connector_y = value("MENU_RAIL_CONNECTOR_Y")
        connector_width = value("MENU_RAIL_CONNECTOR_WIDTH")
        progress_y = value("MENU_RAIL_PROGRESS_Y")
        progress_step = value("MENU_RAIL_PROGRESS_STEP")
        max_dots = value("MENU_RAIL_MAX_DOTS")

        self.assertLessEqual(side_width // 2, previous_x)
        self.assertLessEqual(next_x + side_width // 2, screen_width)
        self.assertLessEqual(side_height // 2, center_y)
        self.assertLessEqual(center_y + side_height // 2, screen_height)
        self.assertLessEqual(selected_width // 2, selected_x)
        self.assertLessEqual(selected_x + selected_width // 2, screen_width)
        self.assertLessEqual(selected_height // 2, center_y)
        self.assertLessEqual(center_y + selected_height // 2, screen_height)
        self.assertLessEqual(connector_x + connector_width, screen_width)
        self.assertLess(connector_y, screen_height)
        self.assertLess(header_divider, center_y - selected_height // 2)
        self.assertGreaterEqual(progress_y, 2)
        self.assertLess(progress_y + 2, screen_height)
        self.assertLessEqual((max_dots - 1) * progress_step + 1, screen_width)

        matrix = function_definition(self.menu, "menu_draw_signal_matrix")
        rail = function_definition(self.menu, "menu_draw_signal_rail")
        wii = function_definition(self.menu, "menu_draw_wii_classic")
        for constant in (
            "MENU_MATRIX_PAGE_SIZE",
            "MENU_MATRIX_ROWS",
            "MENU_MATRIX_CELL_WIDTH",
            "MENU_MATRIX_CELL_HEIGHT",
            "MENU_MATRIX_FIRST_CENTER_X",
            "MENU_MATRIX_FIRST_CENTER_Y",
            "MENU_MATRIX_SELECTED_WIDTH",
            "MENU_MATRIX_SELECTED_HEIGHT",
        ):
            self.assertIn(constant, matrix)
        for constant in (
            "MENU_RAIL_PREVIOUS_X",
            "MENU_RAIL_SELECTED_X",
            "MENU_RAIL_NEXT_X",
            "MENU_RAIL_PROGRESS_STEP",
            "MENU_RAIL_MAX_DOTS",
        ):
            self.assertIn(constant, rail)
        for constant in (
            "MENU_WII_PAGE_SIZE",
            "MENU_WII_ROWS",
            "MENU_WII_COLUMN_STEP",
            "MENU_WII_ROW_STEP",
            "MENU_WII_FIRST_X",
            "MENU_WII_FIRST_Y",
            "MENU_WII_CELL_WIDTH",
            "MENU_WII_CELL_HEIGHT",
            "MENU_WII_ICON_HEIGHT",
            "MENU_WII_LABEL_BASELINE",
            "MENU_WII_LABEL_MAX_WIDTH",
        ):
            self.assertIn(constant, wii)

    def test_empty_and_small_counts_guard_all_item_accesses(self) -> None:
        callback = function_definition(self.menu, "menu_draw_callback")
        empty_guard = callback.index("if(!MenuItemArray_size(model->items))")
        empty_return = callback.index("return;", empty_guard)
        style_switch = callback.index("switch(model->style)")
        self.assertLess(empty_guard, empty_return)
        self.assertLess(empty_return, style_switch)

        rail = function_definition(self.menu, "menu_draw_signal_rail")
        self.assertIn("if(count > 1U)", rail)
        self.assertIn("if(count > 2U)", rail)
        self.assertIn("count > 1U ?", rail)

    def test_navigation_helper_matches_physical_layout_axes_exhaustively(self) -> None:
        compiler = shutil.which("cc") or shutil.which("clang")
        self.assertIsNotNone(compiler, "a host C compiler is required")

        helper = function_definition(self.menu, "menu_next_position")
        dependencies: list[str] = []
        for dependency in ("menu_wrap_position",):
            if re.search(rf"\b{dependency}\s*\(", helper):
                dependencies.append(function_definition(self.menu, dependency))
        helper_defines = "\n".join(
            f"#define {name} {numeric_define(self.menu, name)}U"
            for name in sorted(set(re.findall(r"\bMENU_[A-Z0-9_]+\b", helper)))
        )

        harness = textwrap.dedent(
            f"""
            #include <assert.h>
            #include <stddef.h>
            #include <stdio.h>

            typedef enum {{
                MenuStyleList = 0,
                MenuStyleWii = 1,
                MenuStyleDsi = 2,
                MenuStyleVertical = 3,
                MenuStyleWiiVertical = 4,
                MenuStyleWiiClassic = 5,
                MenuStyleCount = 6,
            }} MenuStyle;

            typedef enum {{
                InputKeyUp,
                InputKeyDown,
                InputKeyRight,
                InputKeyLeft,
                InputKeyOk,
                InputKeyBack,
            }} InputKey;

            {helper_defines}
            {chr(10).join(dependencies)}
            {helper}

            static int key_is_relevant(MenuStyle style, InputKey key) {{
                switch(style) {{
                case MenuStyleWii:
                case MenuStyleWiiClassic:
                case MenuStyleWiiVertical:
                    return key == InputKeyUp || key == InputKeyDown ||
                           key == InputKeyLeft || key == InputKeyRight;
                case MenuStyleDsi:
                case MenuStyleVertical:
                    return key == InputKeyLeft || key == InputKeyRight;
                case MenuStyleList:
                default:
                    return key == InputKeyUp || key == InputKeyDown;
                }}
            }}

            static size_t expected(
                MenuStyle style,
                InputKey key,
                size_t position,
                size_t count) {{
                if(count == 0U) return 0U;
                position %= count;

                switch(style) {{
                case MenuStyleWii:
                case MenuStyleWiiClassic:
                    {{
                    const size_t column = position / 2U;
                    const size_t row = position % 2U;
                    if(key == InputKeyUp || key == InputKeyDown) {{
                        const size_t target = column * 2U +
                                              (key == InputKeyDown ? 1U : 0U);
                        return target < count ? target : position;
                    }}
                    if(key == InputKeyLeft || key == InputKeyRight) {{
                        const size_t row_columns = (count + 1U - row) / 2U;
                        const size_t next_column = key == InputKeyRight ?
                                                       (column + 1U) % row_columns :
                                                       (column + row_columns - 1U) % row_columns;
                        return next_column * 2U + row;
                    }}
                    return position;
                    }}
                case MenuStyleDsi:
                case MenuStyleVertical:
                    if(key == InputKeyLeft) {{
                        return position > 0U ? position - 1U : count - 1U;
                    }}
                    if(key == InputKeyRight) {{
                        return position + 1U < count ? position + 1U : 0U;
                    }}
                    return position;
                case MenuStyleWiiVertical:
                    /* Side Grid receives physical keys and rotates them once. */
                    if(key == InputKeyLeft) {{
                        if(position >= 2U) return position - 2U;
                        const size_t column = position % 2U;
                        const size_t rows_in_column = (count + 1U - column) / 2U;
                        return (rows_in_column - 1U) * 2U + column;
                    }}
                    if(key == InputKeyRight) {{
                        const size_t target = position + 2U;
                        if(target < count) return target;
                        const size_t column = position % 2U;
                        return column < count ? column : 0U;
                    }}
                    if(key == InputKeyUp || key == InputKeyDown) {{
                        const size_t partner = position ^ 1U;
                        return partner < count ? partner : position;
                    }}
                    return position;
                case MenuStyleList:
                default:
                    if(key == InputKeyUp) {{
                        return position > 0U ? position - 1U : count - 1U;
                    }}
                    if(key == InputKeyDown) {{
                        return position + 1U < count ? position + 1U : 0U;
                    }}
                    return position;
                }}
            }}

            int main(void) {{
                const InputKey keys[] = {{
                    InputKeyUp,
                    InputKeyDown,
                    InputKeyLeft,
                    InputKeyRight,
                    InputKeyOk,
                    InputKeyBack,
                }};

                for(size_t style = 0U; style < MenuStyleCount; style++) {{
                    /*
                     * 0/1/2 exercise empty and degenerate menus; the complete
                     * 0..40 range also includes odd, partial-page, exact-page,
                     * multi-page, and Rail's dot/progress boundary counts.
                     */
                    for(size_t count = 0U; count <= 40U; count++) {{
                        for(size_t position = 0U; position <= count + 4U; position++) {{
                            for(size_t k = 0U; k < sizeof(keys) / sizeof(keys[0]); k++) {{
                                const size_t actual = menu_next_position(
                                    (MenuStyle)style, keys[k], position, count);
                                const size_t wanted = expected(
                                    (MenuStyle)style, keys[k], position, count);
                                if(actual != wanted ||
                                   !(count == 0U ? actual == 0U : actual < count)) {{
                                    fprintf(
                                        stderr,
                                        "style=%zu count=%zu position=%zu key=%u actual=%zu expected=%zu\\n",
                                        style,
                                        count,
                                        position,
                                        (unsigned int)keys[k],
                                        actual,
                                        wanted);
                                    return 1;
                                }}

                                if(count > 0U &&
                                   !key_is_relevant((MenuStyle)style, keys[k]) &&
                                   actual != position % count) {{
                                    fprintf(
                                        stderr,
                                        "irrelevant key moved menu: style=%zu count=%zu position=%zu key=%u actual=%zu\\n",
                                        style,
                                        count,
                                        position,
                                        (unsigned int)keys[k],
                                        actual);
                                    return 2;
                                }}

                                if(count > 0U &&
                                   (style == MenuStyleWii || style == MenuStyleWiiClassic)) {{
                                    const size_t normalized = position % count;
                                    if((keys[k] == InputKeyLeft || keys[k] == InputKeyRight) &&
                                       actual % 2U != normalized % 2U) {{
                                        fprintf(stderr, "Matrix horizontal move changed row\\n");
                                        return 3;
                                    }}
                                    if((keys[k] == InputKeyUp || keys[k] == InputKeyDown) &&
                                       actual / 2U != normalized / 2U) {{
                                        fprintf(stderr, "Matrix vertical move changed column\\n");
                                        return 4;
                                    }}
                                }}

                                if(count > 0U && style == MenuStyleWiiVertical) {{
                                    const size_t normalized = position % count;
                                    if((keys[k] == InputKeyLeft || keys[k] == InputKeyRight) &&
                                       actual % 2U != normalized % 2U) {{
                                        fprintf(stderr, "Side Grid horizontal move changed column\\n");
                                        return 5;
                                    }}
                                    if((keys[k] == InputKeyUp || keys[k] == InputKeyDown) &&
                                       actual / 2U != normalized / 2U) {{
                                        fprintf(stderr, "Side Grid vertical move changed row\\n");
                                        return 6;
                                    }}
                                }}
                            }}
                        }}
                    }}
                }}

                return 0;
            }}
            """
        )

        with tempfile.TemporaryDirectory(prefix="tumoflip-menu-navigation-") as directory:
            harness_path = Path(directory) / "menu_navigation_test.c"
            binary = Path(directory) / "menu_navigation_test"
            harness_path.write_text(harness, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    str(compiler),
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(harness_path),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            run_result = subprocess.run(
                [str(binary)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(
                run_result.returncode,
                0,
                run_result.stdout + run_result.stderr,
            )

    def test_runtime_direction_path_uses_the_validated_navigation_helper(self) -> None:
        input_callback = function_definition(self.menu, "menu_input_callback")
        process = function_definition(self.menu, "menu_process_direction")

        self.assertIn("menu_process_direction(menu, event->key);", input_callback)
        self.assertEqual(input_callback.count("menu_process_direction(menu, event->key);"), 1)
        self.assertIn("position = menu_next_position(", process)
        self.assertIn("MenuItemArray_size(model->items)", process)
        for legacy_handler in (
            "menu_process_up",
            "menu_process_down",
            "menu_process_left",
            "menu_process_right",
        ):
            self.assertNotIn(legacy_handler, self.menu)

    def test_noop_direction_always_releases_the_locking_view_model(self) -> None:
        """A no-op direction must still reach view_commit_model() and unlock."""

        setter = function_definition(self.menu, "menu_set_position")
        self.assertNotIn(
            "return;",
            setter,
            "return inside with_view_model bypasses view_commit_model and leaks its mutex",
        )
        self.assertRegex(setter, r"bool\s+(?:changed|update)\s*=\s*false\s*;")
        self.assertRegex(setter, r"if\s*\(count\)\s*{")
        self.assertRegex(setter, r"if\s*\(position\s*!=\s*model->position\)\s*{")
        self.assertIsNotNone(
            re.search(
                r"with_view_model\s*\(.*?\}\s*,\s*(?:changed|update)\s*\)\s*;",
                setter,
                re.DOTALL,
            ),
            "redraw flag must be false for a no-op while the model is still committed",
        )

    def test_runtime_noop_directions_release_model_without_side_effects(self) -> None:
        """Execute the real input path against a locking ViewModel fake."""

        compiler = shutil.which("cc") or shutil.which("clang")
        self.assertIsNotNone(compiler, "a host C compiler is required")
        functions = "\n\n".join(
            function_definition(self.menu, name)
            for name in (
                "menu_wrap_position",
                "menu_next_position",
                "menu_set_position",
                "menu_process_direction",
                "menu_input_callback",
            )
        )
        helper = function_definition(self.menu, "menu_next_position")
        helper_defines = "\n".join(
            f"#define {name} {numeric_define(self.menu, name)}U"
            for name in sorted(set(re.findall(r"\bMENU_[A-Z0-9_]+\b", helper)))
        )
        harness = textwrap.dedent(
            f"""
            #include <assert.h>
            #include <stdbool.h>
            #include <stddef.h>
            #include <stdint.h>
            #include <string.h>

            typedef enum {{
                MenuStyleList = 0,
                MenuStyleWii = 1,
                MenuStyleDsi = 2,
                MenuStyleVertical = 3,
                MenuStyleWiiVertical = 4,
                MenuStyleWiiClassic = 5,
                MenuStyleCount = 6,
            }} MenuStyle;

            typedef enum {{
                InputKeyUp,
                InputKeyDown,
                InputKeyRight,
                InputKeyLeft,
                InputKeyOk,
                InputKeyBack,
            }} InputKey;

            typedef enum {{
                InputTypePress,
                InputTypeRelease,
                InputTypeShort,
                InputTypeLong,
                InputTypeRepeat,
            }} InputType;

            typedef struct {{ InputKey key; InputType type; }} InputEvent;
            typedef struct {{ unsigned int id; }} IconAnimation;
            typedef struct {{ IconAnimation* icon; }} MenuItem;
            typedef struct {{ MenuItem* data; size_t size; }} MenuItemArray_t;
            typedef struct {{
                MenuItemArray_t items;
                size_t position;
                size_t vertical_offset;
                MenuStyle style;
                void* label_scratch;
                size_t label_scratch_capacity;
            }} MenuModel;
            typedef struct {{
                MenuModel model;
                bool locked;
                size_t get_count;
                size_t commit_count;
                size_t update_count;
            }} View;
            typedef struct Menu {{ View* view; }} Menu;

            static size_t icon_start_count;
            static size_t icon_stop_count;
            static size_t ok_count;

            static size_t MenuItemArray_size(MenuItemArray_t items) {{ return items.size; }}
            static MenuItem* MenuItemArray_get(MenuItemArray_t items, size_t position) {{
                assert(position < items.size);
                return &items.data[position];
            }}
            static void icon_animation_start(IconAnimation* icon) {{
                assert(icon);
                icon_start_count++;
            }}
            static void icon_animation_stop(IconAnimation* icon) {{
                assert(icon);
                icon_stop_count++;
            }}
            static void* view_get_model(View* view) {{
                assert(!view->locked);
                view->locked = true;
                view->get_count++;
                return &view->model;
            }}
            static void view_commit_model(View* view, bool update) {{
                assert(view->locked);
                view->locked = false;
                view->commit_count++;
                if(update) view->update_count++;
            }}

            #define with_view_model(view, type, code, update) \
                {{                                                \
                    type = view_get_model(view);                  \
                    {{code}};                                     \
                    view_commit_model(view, update);              \
                }}

            {helper_defines}
            static void menu_process_direction(Menu* menu, InputKey key);
            static void menu_process_ok(Menu* menu) {{
                (void)menu;
                ok_count++;
            }}

            {functions}

            static void reset_fixture(
                View* view,
                Menu* menu,
                MenuItem* items,
                IconAnimation* icons,
                MenuStyle style,
                size_t count,
                size_t position) {{
                memset(view, 0, sizeof(*view));
                memset(items, 0, 9U * sizeof(*items));
                memset(icons, 0, 9U * sizeof(*icons));
                for(size_t i = 0U; i < count; i++) items[i].icon = &icons[i];
                view->model.items.data = items;
                view->model.items.size = count;
                view->model.position = count ? position % count : 0U;
                view->model.style = style;
                menu->view = view;
                icon_start_count = 0U;
                icon_stop_count = 0U;
                ok_count = 0U;
            }}

            int main(void) {{
                const InputKey keys[] = {{
                    InputKeyUp, InputKeyDown, InputKeyLeft, InputKeyRight
                }};
                const InputType types[] = {{InputTypeShort, InputTypeRepeat}};

                for(size_t style = 0U; style < MenuStyleCount; style++) {{
                    for(size_t count = 0U; count <= 9U; count++) {{
                        const size_t positions = count ? count : 1U;
                        for(size_t position = 0U; position < positions; position++) {{
                            for(size_t key_i = 0U; key_i < 4U; key_i++) {{
                                const size_t expected = menu_next_position(
                                    (MenuStyle)style, keys[key_i], position, count);
                                for(size_t type_i = 0U; type_i < 2U; type_i++) {{
                                    View view;
                                    Menu menu;
                                    MenuItem items[9];
                                    IconAnimation icons[9];
                                    reset_fixture(
                                        &view,
                                        &menu,
                                        items,
                                        icons,
                                        (MenuStyle)style,
                                        count,
                                        position);
                                    InputEvent event = {{.key = keys[key_i], .type = types[type_i]}};
                                    assert(menu_input_callback(&event, &menu));
                                    assert(!view.locked);
                                    assert(view.get_count == 2U);
                                    assert(view.commit_count == 2U);
                                    assert(view.model.position == expected);
                                    const bool moved = count && expected != position;
                                    assert(view.update_count == (moved ? 1U : 0U));
                                    assert(icon_start_count == (moved ? 1U : 0U));
                                    assert(icon_stop_count == (moved ? 1U : 0U));
                                    assert(ok_count == 0U);
                                }}
                            }}
                        }}
                    }}
                }}

                View view;
                Menu menu;
                MenuItem items[9];
                IconAnimation icons[9];
                reset_fixture(
                    &view, &menu, items, icons, MenuStyleDsi, 3U, 1U);
                InputEvent no_op = {{.key = InputKeyUp, .type = InputTypeRepeat}};
                for(size_t i = 0U; i < 10000U; i++) {{
                    assert(menu_input_callback(&no_op, &menu));
                }}
                assert(!view.locked);
                assert(view.update_count == 0U);
                assert(icon_start_count == 0U && icon_stop_count == 0U);
                return 0;
            }}
            """
        )

        with tempfile.TemporaryDirectory(prefix="tumoflip-menu-runtime-lock-") as directory:
            harness_path = Path(directory) / "menu_runtime_lock_test.c"
            binary = Path(directory) / "menu_runtime_lock_test"
            harness_path.write_text(harness, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    str(compiler),
                    "-std=c11",
                    "-O0",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(harness_path),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            run_result = subprocess.run(
                [str(binary)],
                capture_output=True,
                text=True,
                check=False,
                timeout=3.0,
            )
            self.assertEqual(
                run_result.returncode,
                0,
                run_result.stdout + run_result.stderr,
            )

    def test_side_grid_rotates_each_physical_direction_exactly_once(self) -> None:
        """Side Grid draws on a rotated canvas, so its physical D-pad is rotated too."""

        navigation = function_definition(self.menu, "menu_next_position")
        input_callback = function_definition(self.menu, "menu_input_callback")
        self.assertNotIn("MenuStyleWiiVertical", input_callback)
        remap_start = navigation.index("if(style == MenuStyleWiiVertical)")
        remap_end = navigation.index(
            "if(style == MenuStyleWii || style == MenuStyleWiiClassic)", remap_start
        )
        remap_region = navigation[remap_start:remap_end]
        remaps = dict(
            re.findall(
                r"case\s+(InputKey(?:Up|Down|Left|Right))\s*:"
                r"\s*key\s*=\s*(InputKey(?:Up|Down|Left|Right))\s*;",
                remap_region,
            )
        )
        self.assertEqual(
            remaps,
            {
                "InputKeyUp": "InputKeyRight",
                "InputKeyDown": "InputKeyLeft",
                "InputKeyLeft": "InputKeyUp",
                "InputKeyRight": "InputKeyDown",
            },
        )

    def test_matrix_pages_are_bounded_and_keep_the_selected_item_visible(self) -> None:
        compiler = shutil.which("cc") or shutil.which("clang")
        self.assertIsNotNone(compiler, "a host C compiler is required")

        helper = function_definition(self.menu, "menu_matrix_first")
        page_size = numeric_define(self.menu, "MENU_MATRIX_PAGE_SIZE")
        harness = textwrap.dedent(
            f"""
            #include <assert.h>
            #include <stddef.h>

            #define MENU_MATRIX_PAGE_SIZE {page_size}U
            {helper}

            int main(void) {{
                const size_t capacity = 6U;
                const size_t counts[] = {{0U, 1U, 2U, 5U, 6U, 7U, 8U, 11U, 12U}};

                for(size_t c = 0U; c < sizeof(counts) / sizeof(counts[0]); c++) {{
                    const size_t count = counts[c];
                    for(size_t position = 0U; position < 12U; position++) {{
                        const size_t first = menu_matrix_first(position, count);
                        if(count == 0U) {{
                            assert(first == 0U);
                            continue;
                        }}

                        const size_t normalized = position % count;
                        const size_t expected = (normalized / capacity) * capacity;
                        assert(first == expected);
                        assert(first <= normalized);
                        assert(normalized < first + capacity);
                        assert(first < count);
                        assert(first % capacity == 0U);
                    }}

                    if(count > capacity) {{
                        const size_t last = count - 1U;
                        const size_t first = menu_matrix_first(last, count);
                        assert(first <= last);
                        assert(last < first + capacity);
                    }}
                }}
                return 0;
            }}
            """
        )

        with tempfile.TemporaryDirectory(prefix="tumoflip-menu-paging-") as directory:
            harness_path = Path(directory) / "menu_paging_test.c"
            binary = Path(directory) / "menu_paging_test"
            harness_path.write_text(harness, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    str(compiler),
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(harness_path),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            run_result = subprocess.run(
                [str(binary)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(
                run_result.returncode,
                0,
                run_result.stdout + run_result.stderr,
            )

    def test_wii_renderer_uses_its_own_sliding_window(self) -> None:
        callback = function_definition(self.menu, "menu_draw_callback")
        renderer = function_definition(self.menu, "menu_draw_wii_classic")

        self.assertRegex(
            callback,
            r"case\s+MenuStyleWiiClassic\s*:\s*menu_draw_wii_classic\s*\(",
        )
        self.assertIn("menu_wii_first(position, count)", renderer)
        self.assertIn("MENU_WII_PAGE_SIZE", renderer)
        self.assertIn("MENU_WII_ROWS", renderer)

    def test_wii_window_is_bounded_and_keeps_the_selected_item_visible(self) -> None:
        """The restored Wii 3x2 grid scrolls by whole two-item columns."""

        compiler = shutil.which("cc") or shutil.which("clang")
        self.assertIsNotNone(compiler, "a host C compiler is required")

        helper = function_definition(self.menu, "menu_wii_first")
        page_size = numeric_define(self.menu, "MENU_WII_PAGE_SIZE")
        rows = numeric_define(self.menu, "MENU_WII_ROWS")
        self.assertEqual((page_size, rows), (6, 2))
        harness = textwrap.dedent(
            f"""
            #include <assert.h>
            #include <stddef.h>

            #define MENU_WII_PAGE_SIZE {page_size}U
            #define MENU_WII_ROWS {rows}U
            {helper}

            static size_t expected_first(size_t position, size_t count) {{
                if(count == 0U || count <= MENU_WII_PAGE_SIZE) return 0U;
                position %= count;
                if(position < MENU_WII_PAGE_SIZE - MENU_WII_ROWS) return 0U;

                const size_t column_first = position - (position % MENU_WII_ROWS);
                const size_t tail_threshold =
                    count - MENU_WII_ROWS + (count % MENU_WII_ROWS);
                return position >= tail_threshold ?
                           column_first - (MENU_WII_PAGE_SIZE - MENU_WII_ROWS) :
                           column_first - MENU_WII_ROWS;
            }}

            int main(void) {{
                assert(MENU_WII_PAGE_SIZE == 6U);
                assert(MENU_WII_ROWS == 2U);

                for(size_t count = 0U; count <= 40U; count++) {{
                    for(size_t position = 0U; position <= count + 4U; position++) {{
                        const size_t first = menu_wii_first(position, count);
                        assert(first == expected_first(position, count));

                        if(count == 0U) {{
                            assert(first == 0U);
                            continue;
                        }}

                        const size_t normalized = position % count;
                        assert(first <= normalized);
                        assert(normalized < first + MENU_WII_PAGE_SIZE);
                        assert(first < count);
                        assert(first % MENU_WII_ROWS == 0U);
                        if(count <= MENU_WII_PAGE_SIZE) assert(first == 0U);
                    }}

                    if(count > 0U) {{
                        size_t previous_first = menu_wii_first(0U, count);
                        for(size_t position = 1U; position < count; position++) {{
                            const size_t first = menu_wii_first(position, count);
                            assert(first >= previous_first);
                            assert(first - previous_first <= MENU_WII_ROWS);
                            if(position % MENU_WII_ROWS != 0U) {{
                                assert(first == previous_first);
                            }}
                            previous_first = first;
                        }}
                    }}

                    if(count > MENU_WII_PAGE_SIZE) {{
                        const size_t first = menu_wii_first(count - 1U, count);
                        assert(first + MENU_WII_PAGE_SIZE >= count);
                    }}
                }}
                return 0;
            }}
            """
        )

        with tempfile.TemporaryDirectory(prefix="tumoflip-menu-wii-window-") as directory:
            harness_path = Path(directory) / "menu_wii_window_test.c"
            binary = Path(directory) / "menu_wii_window_test"
            harness_path.write_text(harness, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    str(compiler),
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(harness_path),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            run_result = subprocess.run(
                [str(binary)], capture_output=True, text=True, check=False
            )
            self.assertEqual(
                run_result.returncode,
                0,
                run_result.stdout + run_result.stderr,
            )


if __name__ == "__main__":
    unittest.main()
