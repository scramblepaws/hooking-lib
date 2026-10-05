# Building custom menus

This guide documents the current widget stack (`src/ui/`) as it exists today: a retained widget layer, a backend-agnostic Canvas, and replaceable painters. The demo Ragebot page is the reference implementation.

For the public product surface, see `include/overlay/overlay_c.h` and the `examples/sdk_consumer` example: it links the DLL, builds a menu through the C ABI, and uses **blank mode** (its own chrome painter) - `overlay_menu_set_chrome_painter`. Without a chrome painter the library renders the **starter chrome** (sidebar, topbar, cards) and lays sections out in two balanced columns.

Configs are named JSON snapshots keyed by widget `id`, stored in `<dll dir>/configs/<name>.json`. `overlay_config_save/load/delete/list` expose them through the C ABI; the starter menu's Configs page wires a name field to Save/Load, and eject autosaves the active config.

## External mode

`overlay_init` with `OVERLAY_FLAG_EXTERNAL` skips hooking entirely: the library creates its own topmost, color-keyed window (over `target_hwnd`'s client area, or the primary monitor) and runs the UI and script stack on its own thread. The window is click-through while the menu is closed and becomes interactive (focus + cursor) while it is open; `Del` toggles, `End` shuts the overlay down. See `examples/external_host`. Menu content comes from the scripts folder (or `overlay_script_execute`); `overlay_shutdown()` stops the thread.

Note for host-linked applications: auto-init in `DllMain` waits briefly for a host to claim initialization, so calling any `overlay_init` entry point from `main()` always wins over injected-mode startup.

Styling is a CSS subset loaded from `assets/theme.css` (or `overlay_style_load`/`overlay_style_parse`). `menu {}` sets global colors, `class {}` a widget class (`toggle`, `slider`, `dropdown`, `checkbox`, `keybind`, `text-input`, `color-picker`, `button`), `#id {}` one widget instance; id beats class beats menu. Properties: `accent`, `track`, `knob`, `text`, `text-dim`, `panel`, `border`, `window-bg`, `row-hover`, `nav-selected`, `button`, `button-hover`, `field`, `rounding`. Values: `#rgb`, `#rrggbb`, `#rrggbbaa`, `rgb(r,g,b)`, `rgba(r,g,b,a)`.

```css
menu { accent: #3fd23f; rounding: 8; }
toggle { track: #202626; }
#enabled { knob: #ffffff; }
```

Toasts and the event feed are script/dev facing: `overlay_notify("...")`, `overlay_event("...")`, `overlay_toast_position(...)`, `overlay_toast_duration(...)`.

## Scripting (Luau)

Every `*.lua` in `<dll dir>/scripts/` runs at startup on a sandboxed VM and is hot-reloaded on change. Scripts own the menu when at least one exists; with no scripts the Overlay stays empty until the host adds pages through the C ABI. Buttons with ids `config_save` / `config_load`, together with a `config_name` text input, get config handling from the library on any page.

```lua
local page = ui.page("Home", "Root")
local section = ui.section(page, "MAIN")
ui.toggle(section, "enabled", "Enabled", false)
ui.slider(section, "speed", "Speed", 50, 0, 100, "%.0f%%")
ui.button(section, "panic", "Panic")
ui.on_click("panic", function()
  ui.set_bool("enabled", false)
  ui.notify("panic fired")
end)
ui.set_painter("enabled", function(state, canvas)
  local s = canvas:scale()
  canvas:rect(state.cx, state.cy, state.cw, state.ch, state.value and "#3fd23f" or "#202626", 4 * s)
end)
```

- Creation: `ui.page(title, nav_section)`, `ui.section(page, title)`, then `ui.toggle/checkbox/slider/dropdown/keybind/text_input/colorpicker/button(section, id, label, ...)`. Ids are the config keys. Colors (colorpicker, canvas painters) accept a hex string (`"#8c5cf5"`), a `Color3` table, or four raw numbers.
- Properties: `ui.get_bool/set_bool`, `get/set_number`, `get/set_int`, `get/set_text`, `get/set_key`, `get/set_color` (hex string or r,g,b,a).
- Painters: `ui.set_painter(id, function(state, canvas) ...)`; state exposes `value, hovered, focused, clicked, anim`, row rect `x,y,w,h` and control rect `cx,cy,cw,ch`. Canvas methods: `rect`, `outline`, `circle`, `text`, `text_centered`, `measure`, `scale`, `clip_push`, `clip_pop`; colors accept `"#rrggbb"`, `"#rgb"`, `"#rrggbbaa"` or four numbers.
- Scheduling: `task.wait(s)`, `task.spawn(fn, ...)`, `task.defer`, `task.delay(s, fn)`; scripts run on the render thread with a 5 ms per-resume budget and an interrupt that stops runaway loops.
- Sandbox: `os`, `io`, `debug`, `require`, `load`, `loadstring`, `dofile`, `collectgarbage` are removed and globals are frozen; `print` goes to `overlay.log`; `fs.read(name)` / `fs.write(name, text)` are scoped to the `configs/` folder.
- `ui.save_config(name)`, `ui.load_config(name)`, `ui.notify(msg)`, `ui.event(msg)` close the loop with the Config and notification systems.
- Embedding: `overlay_script_execute(source, chunk_name)` runs a chunk from C; `overlay_script_reload()` forces a reload.

## Drawing (ESP primitives)

`Drawing` renders over the game above regular UI, whether or not the menu is open. Objects are persistent; set properties, then `:Remove()` when done. `examples/scripts/drawing_demo.lua` is a runnable sample.

```lua
local box = Drawing.new("Square")
box.Visible = true
box.Position = Vector2.new(560, 250)
box.Size = Vector2.new(160, 220)
box.Thickness = 2
box.Color = Color3.fromRGB(255, 70, 70)

local label = Drawing.new("Text")
label.Visible = true
label.Position = Vector2.new(640, 200)
label.Center = true
label.Size = 18
label.Outline = true
label.Text = "target"
label.Color = Color3.new(1, 1, 1)
```

- Kinds: `Line` (`From`, `To`), `Text` (`Position`, `Text`, `Size`, `Center`, `Outline`), `Circle` (`Position`, `Radius`, `Filled`), `Square` (`Position`, `Size`, `Filled`), `Triangle` (`PointA`, `PointB`, `PointC`, `Filled`), `Image` (`Data` path, `Position`, `Size`). Images decode via Windows WIC; relative paths resolve against the scripts folder, upload to the active backend (DX11, DX12 or Vulkan), and the cache self-heals across device loss.
- Common properties: `Visible`, `ZIndex`, `Color` (Color3 or hex string), `Transparency` (0..1), `Thickness`, `:Remove()` / `:Destroy()`.
- `Vector2.new(x, y)` and `Color3.new/fromRGB/fromHSV` are provided by the sandbox; `Color3` values are 0..1. Coordinates are raw target-client pixels.
- Drawing objects are cleared on script reload.

Honest status: the widget layer is reusable, the menu chrome (sidebar, topbar, cards) is menu-specific code you copy and edit. The public C ABI exists; the C++ headers remain in-tree for now.

## The three layers

| Layer | File | Responsibility |
|---|---|---|
| Canvas | `canvas.h/.cpp` | Drawing only: rects, circles, triangles, text, clipping. No input, no state. |
| Widget | `widget.h/.cpp` | One persistent UI element: value, state, hit-testing, drag, capture, layout rect. Behavior is ours. |
| Painter | `painter` field | Draws one Widget into a Canvas. Default per class; replace per widget to change the look. |

Frame flow (see `menu.cpp: draw()`):

1. `build_widgets()` once - creates every Widget and its value.
2. `layout_*()` every frame - assigns `widget.rect` (absolute screen coords).
3. `widget_update(widget, input)` - behavior; sets `input.click_consumed` when it handles a click.
4. `widget_paint(widget, canvas)` - look.
5. `widget_paint_popup(widget, canvas)` last - dropdown / colorpicker popups draw above everything.

## Widgets

Factory functions (in `widget.h`), value type per class:

```cpp
Widget make_toggle(id, label, bool value);
Widget make_checkbox(id, label, bool value);
Widget make_slider(id, label, float value, float min, float max, const char* format,
                   const char* alt_text = nullptr);   // alt_text shown at min, e.g. "Auto"
Widget make_dropdown(id, label, int index, std::vector<std::string> items);
Widget make_keybind(id, label, int code);              // 0 none, >0 VK, <0 mouse (-1 left ...)
Widget make_textinput(id, label, const char* value);
Widget make_colorpicker(id, label, Color color);
```

Read values without knowing the variant:

```cpp
bool on        = widget_bool(w);
float fov      = widget_float(w);
int   hitbox   = widget_int(w);
const std::string& name = widget_string(w);
Color tracer   = widget_color(w);
int   key      = widget_key(w).code;
char  name[32]; key_name(key, name, sizeof(name));     // "Mouse5", "F1", "Space", ...
```

Per-widget layout/behavior knobs:

- `show_label`, `show_gear` - painter draws label left, gear icon before the control
- `control_width` - override the default control width in base px (multiplied by scale)
- `rect` - full row rect, assigned by your layout
- `state` - `hovered`, `held`, `open`, `focused`, `capturing`, `anim`, `blink`, `hue`/`sv_s`/`sv_v`, `popup`

`id` is stable identity - it will be the Config key when persistence lands. Keep it unique.

## Canvas

All geometry is in pixels, already scaled. Multiply base sizes by `canvas.scale()`:

```cpp
void paint_my_thing(const Widget& w, Canvas& c) {
  const float s = c.scale();
  const Rect control = widget_control_rect(w, s);   // right-aligned control rect
  c.fill_rect(control, {0.70f, 0.21f, 0.21f, 1.0f}, 3.0f * s);
  c.text_centered(control, {1, 1, 1, 1}, "custom");
}
```

Methods: `scale`, `measure_text`, `measure_text_sized`, `fill_rect(rect, color, radius)`, `stroke_rect(rect, color, thickness, radius)`, `fill_circle`, `stroke_circle`, `fill_triangle`, `text(pos, color, str)`, `text_sized(pos, color, size, str)`, `text_centered(rect, color, str)`, `push_clip(rect)`, `pop_clip()`.

Types: `Rect{x,y,w,h}` with `right()`, `bottom()`, `contains(Vec2)`; `Color{r,g,b,a}` floats 0..1.

## Cookbook

### Add a widget row

```cpp
g_main.push_back(make_toggle("wallbang", "Wallbang", false));
```

That is all - card height is computed from the row count and `layout_card` places it. The update/paint loops pick it up automatically.

### Change the look of one widget (custom painter)

```cpp
g_main.back().painter = [](const Widget& w, Canvas& c) {
  const float s = c.scale();
  const Rect control = widget_control_rect(w, s);
  c.fill_rect(control, widget_bool(w) ? Color{0.3f, 0.9f, 0.4f, 1} : Color{0.15f, 0.15f, 0.17f, 1}, 2 * s);
  c.text_centered(control, {1, 1, 1, 1}, widget_bool(w) ? "ON" : "OFF");
};
```

The painter replaces the entire visual, including the label and gear - draw those yourself if you want them. Behavior, hit-testing and animation state stay with the framework. For dropdowns/colorpickers, a custom painter also takes over popup drawing (`widget_paint_popup` skips custom-painted widgets).

### Retheme everything

- Colors: `src/ui/palette.h` (shared by chrome and default painters)
- Fonts: drop a TTF into `assets/fonts/`, adjust the loader in `menu.cpp: init()`
- Row metrics, paddings, card geometry: constants at the top of `src/ui/menu.cpp`
- Control sizes (`kToggleWidth`, ...): `src/ui/widget.cpp`

### Custom chrome / a different menu structure

Chrome is plain Canvas code in `menu.cpp`: `paint_sidebar`, `paint_topbar`, `paint_cards`, `paint_profile`. To build a different menu, copy `menu.cpp` as a template and rewrite those functions and `layout_board`. The widget layer does not care about your layout - it only needs `widget.rect` set before update/paint.

If chrome needs to react to clicks, follow the same contract as the sidebar: check `input.mouse_clicked && !input.click_consumed`, then set `input.click_consumed = true`.

### Hotkeys

`kToggleKey` / `kEjectKey` in `src/hook/hook.cpp`. Hotkeys are polled with `GetAsyncKeyState` every Present frame (game-proof), not read from window messages.

## Input contract

`Input` is assembled once per frame in `menu.cpp: draw()`:

- `click_consumed` - single-consumer flag. Widgets update first, then chrome, then menu dragging. If you consume a click, later stages ignore it.
- `focus_target` - a text input sets this when clicked; the menu resolves exactly one `focused` widget after the update pass, or clears focus on an unconsumed click.
- `captured_key` - latest raw input event (VK code or negative mouse button). Keybind widgets consume it while capturing; the menu clears it at the end of every frame. Characters arrive via `chars[]`/`char_count`.
- `bounds` - the menu rect; popups flip above their control when they would overflow it.
- `frame` - used by keybind arming so the arming click cannot bind itself.

If you write your own frame loop instead of `menu.cpp: draw()`, you must drain `state().captured_key` and `state().char_count` every frame. While the menu is closed, `render::frame` clears them so nothing goes stale.

## Current limitations

- Internal headers only; nothing is exported from the DLL. Keep the code in-tree or copy `src/ui/` into your project.
- One menu instance; `ui::draw()` is the only entry point and it draws the demo page structure.
- No Config persistence yet - widget values live for the process lifetime. `id` is the key for the planned Config layer.
- TextInput: append/backspace only, 48 chars, no selection or caret movement.
- ColorPicker edits RGB via HSV; no alpha editing.
- Dropdown lists do not scroll; keep them short.
- No keyboard navigation; mouse only.
- The Save button is a placeholder.
- Scale policy: physical DPI, clamped so the 1260x800-base layout fits the target viewport. `OVERLAY_UI_SCALE` overrides it.

## What is coming

1. Luau Script Host - scripts create/manage Widgets and can register Painters (the Canvas above is what they will draw into).
2. Config persistence keyed by widget `id`.
3. Stylesheet layer on top of the default painters.
4. Toasts + Event Feed.
