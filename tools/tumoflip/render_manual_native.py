"""Render actual Advanced scene state with stock 128x64 firmware GUI callbacks.

The native scene fixture supplies labels, values, bounds and choices; production
VariableItemList/NumberInput callbacks and U8g2 fonts supply the pixels. This is
not an emulator or an on-device screenshot.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

from PIL import Image, ImageDraw
from tools.tumoflip import render_weather_native as gui
from tools.tumoflip import test_manual_parameters as fixture

ROOT = Path(__file__).resolve().parents[2]


def states():
    bodies = []
    with patch.object(fixture, "native", bodies.append):
        fixture.ManualParametersTests(
            "test_editor_rejects_bad_values_and_releases_number_view_once"
        ).test_editor_rejects_bad_values_and_releases_number_view_once()
    assert len(bodies) == 1
    production = bodies[0].split("int main(void)", 1)[0]
    driver = r'''
static void list_state(const char*name,VariableItemList*l,unsigned selected){
 printf("LIST\t%s\t%u\n",name,selected);
 for(unsigned i=0;i<l->count;i++)printf("%s\t%s\t%u\t%u\n",l->items[i].label,l->items[i].text,l->items[i].count,l->items[i].index);
}
static void number_state(const char*name){
 assert(manual_input);printf("NUMBER\t%s\t%s\t%d\t%d\t%d\n",name,manual_input->header,manual_input->value,manual_input->min,manual_input->max);
}
int main(void){
 GenInfo g={.type=GenData,.mod="AM650",.freq=433920000,.data={"Princeton",400,30}};
 VariableItemList list={0};SubGhz s={.gen_info=&g,.variable_item_list=&list};
 subghz_scene_set_manual_params_on_enter(&s);list_state("princeton-default",&list,0);
 list.items[0].index=8;list.items[0].change(&list.items[0]);list_state("radio-longest",&list,0);
 list.enter(&s,1);number_state("princeton-te-default");
 manual_input->callback(&s,109);subghz_scene_set_manual_params_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeCustom,last_event});
 list.enter(&s,1);number_state("princeton-te-min");
 manual_input->callback(&s,689);subghz_scene_set_manual_params_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeCustom,last_event});
 list.enter(&s,1);number_state("princeton-te-max");
 list.enter(&s,2);manual_input->callback(&s,72);subghz_scene_set_manual_params_on_event(&s,(SceneManagerEvent){SceneManagerEventTypeCustom,last_event});
 list.enter(&s,2);number_state("princeton-gt-max");
 list_state("princeton-edited",&list,3);subghz_scene_set_manual_params_on_exit(&s);
 g.data.name="Faac TM";g.data.te=999;subghz_scene_set_manual_params_on_enter(&s);list_state("faac-tm",&list,1);
 list.enter(&s,1);number_state("faac-te-max");subghz_scene_set_manual_params_on_exit(&s);
 g.type=GenKeeloq;subghz_scene_set_manual_params_on_enter(&s);list_state("fixed-timing",&list,2);subghz_scene_set_manual_params_on_exit(&s);
 return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="manual-scene-state-") as directory:
        temporary = Path(directory)
        source, binary = temporary / "state.c", temporary / "state"
        source.write_text(production + driver)
        subprocess.run(["cc", "-std=c11", "-w", "-fsanitize=address,undefined",
                        str(source), "-o", str(binary)], check=True)
        lines = iter(subprocess.check_output([str(binary)], text=True).splitlines())
    result = []
    for line in lines:
        fields = line.split("\t")
        if fields[0] == "LIST":
            rows = []
            for _ in range(4):
                label, value, count, index = next(lines).split("\t")
                rows.append({"label": label, "value": value, "count": int(count), "index": int(index)})
            result.append({"name": fields[1], "kind": "list", "selected": int(fields[2]), "rows": rows})
        else:
            assert fields[0] == "NUMBER"
            result.append({"name": fields[1], "kind": "number", "header": fields[2],
                           "value": int(fields[3]), "min": int(fields[4]), "max": int(fields[5])})
    return result


def render(output):
    captured = states()
    production = gui.build_source(None, skip_text_input=True).split("int main(int argc,char** argv)", 1)[0]
    # Weather has an app-local list; this scene uses the real stock widget.
    stock = (ROOT / "applications/services/gui/modules/variable_item_list.c").read_text()
    signature = "static void variable_item_list_draw_callback("
    production = production.replace(gui.function(production, signature), gui.function(stock, signature), 1)
    frames = []
    for state in captured:
        if state["kind"] == "list":
            rows = "\n".join(
                "{VariableItem item={.label=" + json.dumps(row["label"]) +
                ",.current_value_text=furi_string_alloc_set(" + json.dumps(row["value"]) +
                f'),.values_count={row["count"]},.current_value_index={row["index"]}' +
                "};assert(canvas_string_width(&canvas,item.label)<=66);" +
                "assert(canvas_string_width(&canvas,furi_string_get_cstr(item.current_value_text))<=37);" +
                "VariableItemArray_push_back(model.items,item);}"
                for row in state["rows"])
            frames.append("{canvas_set_font(&canvas,FontSecondary);VariableItemListModel model={0};VariableItemArray_init(model.items);" +
                          f'model.position={state["selected"]};' + rows +
                          'canvas_clear(&canvas);variable_item_list_draw_callback(&canvas,&model);' +
                          'save(&canvas,argv[1],' + json.dumps(state["name"]) + ');' +
                          'for(unsigned i=0;i<4;i++)furi_string_free(VariableItemArray_get(model.items,i)->current_value_text);' +
                          'VariableItemArray_clear(model.items);}')
        else:
            frames.append("{NumberInputModel model={.header=furi_string_alloc_set(" + json.dumps(state["header"]) +
                          '),.text_buffer=furi_string_alloc_set(' + json.dumps(str(state["value"])) +
                          f'),.current_number={state["value"]},.min_value={state["min"]},.max_value={state["max"]}' +
                          '};canvas_clear(&canvas);number_input_view_draw_callback(&canvas,&model);' +
                          'save(&canvas,argv[1],' + json.dumps(state["name"]) + ');' +
                          'furi_string_free(model.header);furi_string_free(model.text_buffer);}')
    driver = r'''
int main(int argc,char**argv){assert(argc==2);Canvas canvas={0};static uint8_t buffer[1024];
 static const u8x8_display_info_t info={.tile_width=16,.tile_height=8,.pixel_width=128,.pixel_height=64};
 canvas.fb.u8x8.display_info=&info;u8g2_SetupBuffer(&canvas.fb,buffer,8,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);
 canvas_set_font(&canvas,FontSecondary);canvas_set_color(&canvas,ColorBlack);
''' + "\n".join(frames) + "\nreturn 0;}"
    with tempfile.TemporaryDirectory(prefix="manual-native-render-") as directory:
        temporary = Path(directory)
        source, binary = temporary / "render.c", temporary / "render"
        source.write_text(production + driver)
        library = [p for p in (ROOT / "lib/u8g2").glob("*.c") if p.name != "u8g2_glue.c"]
        gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
        subprocess.run(["cc", "-std=c11", "-D_DEFAULT_SOURCE", "-O1", "-g", "-w",
                        "-fsanitize=address,undefined", "-ffunction-sections", gc,
                        "-I", str(ROOT / "lib/u8g2"), "-I", str(ROOT / "lib/mlib"),
                        str(source), *map(str, library), "-lm", "-o", str(binary)], check=True)
        subprocess.run([str(binary), str(output.resolve())], check=True,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
    sheet = Image.new("RGB", (552, 5 * 170), "#eee")
    draw = ImageDraw.Draw(sheet)
    for index, state in enumerate(captured):
        image = Image.open(output / (state["name"] + ".pgm")).convert("RGB")
        image.save(output / (state["name"] + ".png"))
        x, y = (index % 2) * 276, (index // 2) * 170
        draw.text((x + 8, y + 6), state["name"], fill="black")
        sheet.paste(image.resize((256, 128), Image.Resampling.NEAREST), (x + 8, y + 26))
    sheet.save(output / "advanced-contact-sheet.png")
    (output / "states.json").write_text(json.dumps(captured, indent=2) + "\n")
    print(f"Rendered {len(captured)} production-state frames to {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    render(args.output)


if __name__ == "__main__":
    main()
