#ifndef OVERLAY_OVERLAY_C_H
#define OVERLAY_OVERLAY_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#if defined(OVERLAY_BUILD)
#define OVERLAY_API __declspec(dllexport)
#else
#define OVERLAY_API __declspec(dllimport)
#endif
#else
#define OVERLAY_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define OVERLAY_VERSION_MAJOR 0
#define OVERLAY_VERSION_MINOR 1
#define OVERLAY_VERSION_PATCH 0

/* Incremented on any ABI-breaking change. Stable from 1.0 onward. */
#define OVERLAY_ABI_VERSION 0u

/*
 * OverlayInitInfo.flags:
 * OVERLAY_FLAG_EXTERNAL creates the overlay's own topmost, click-through
 * window instead of hooking a Target. `target_hwnd` optionally sizes it to
 * that window; otherwise it covers the primary monitor.
 */
#define OVERLAY_FLAG_EXTERNAL 0x00000001u

typedef struct OverlayContext OverlayContext;
typedef struct OverlayMenu OverlayMenu;
typedef struct OverlayPage OverlayPage;
typedef struct OverlaySection OverlaySection;
typedef struct OverlayWidget OverlayWidget;
typedef struct OverlayCanvas OverlayCanvas;

typedef enum OverlayStatus {
  OVERLAY_OK = 0,
  OVERLAY_ERR_ALREADY_INITIALIZED = 1,
  OVERLAY_ERR_NOT_INITIALIZED = 2,
  OVERLAY_ERR_HOOK_FAILED = 3,
  OVERLAY_ERR_BACKEND_UNAVAILABLE = 4,
  OVERLAY_ERR_INVALID_ARGUMENT = 5,
  OVERLAY_ERR_OUT_OF_MEMORY = 6,
  OVERLAY_ERR_IO = 7,
  OVERLAY_ERR_NOT_SUPPORTED = 8,
  OVERLAY_ERR_INTERNAL = 9,
  OVERLAY_STATUS_COUNT = 10
} OverlayStatus;

typedef enum OverlayBackend {
  OVERLAY_BACKEND_AUTO = 0,
  OVERLAY_BACKEND_DX11 = 1,
  OVERLAY_BACKEND_DX12 = 2,
  OVERLAY_BACKEND_VULKAN = 3,
  OVERLAY_BACKEND_COUNT = 4
} OverlayBackend;

typedef enum OverlayWidgetKind {
  OVERLAY_WIDGET_TOGGLE = 0,
  OVERLAY_WIDGET_CHECKBOX = 1,
  OVERLAY_WIDGET_SLIDER = 2,
  OVERLAY_WIDGET_DROPDOWN = 3,
  OVERLAY_WIDGET_KEYBIND = 4,
  OVERLAY_WIDGET_TEXT_INPUT = 5,
  OVERLAY_WIDGET_COLOR_PICKER = 6,
  OVERLAY_WIDGET_BUTTON = 7,
  OVERLAY_WIDGET_KIND_COUNT = 8
} OverlayWidgetKind;

typedef struct OverlayVec2 {
  float x, y;
} OverlayVec2;

typedef struct OverlayRect {
  float x, y, w, h;
} OverlayRect;

typedef struct OverlayColor {
  float r, g, b, a;
} OverlayColor;

/*
 * Every public struct starts with `size` = sizeof(the struct). The library
 * reads only the fields covered by `size`, so structs grow additively.
 */
typedef struct OverlayInitInfo {
  uint32_t size;
  OverlayBackend backend;
  uint32_t flags;
  void* target_hwnd;
  const char* menu_title;
} OverlayInitInfo;

typedef void (*OverlayPaintFn)(void* user, const OverlayWidget* widget, OverlayCanvas* canvas);

/*
 * Replaces the default menu chrome. When set, the library stops drawing and
 * positioning the starter chrome; the painter receives the menu and must both
 * draw its own chrome and assign widget rects via overlay_widget_set_rect.
 * Widget behavior, painting and popups still run through the library.
 */
typedef void (*OverlayChromePaintFn)(void* user, OverlayCanvas* canvas, OverlayMenu* menu);

typedef void (*OverlayConfigListFn)(void* user, const char* name);

/* ---- lifecycle ------------------------------------------------------ */

OVERLAY_API uint32_t overlay_abi_version(void);

/* Returned pointer has process lifetime; do not free. */
OVERLAY_API const char* overlay_version_string(void);

/*
 * Installs hooks and prepares rendering. Injected builds call this
 * automatically; host applications call it explicitly. Idempotent.
 */
OVERLAY_API OverlayStatus overlay_init(const OverlayInitInfo* info);
OVERLAY_API OverlayStatus overlay_shutdown(void);

/* Non-zero once a renderer is attached to a target. */
OVERLAY_API int overlay_is_ready(void);
OVERLAY_API int overlay_set_menu_open(int open);
OVERLAY_API int overlay_is_menu_open(void);

/* The active context. Valid for the process lifetime once the module is loaded. */
OVERLAY_API OverlayContext* overlay_context(void);

/* ---- menu ----------------------------------------------------------- */

/* The active menu. Valid for the process lifetime once the module is loaded. */
OVERLAY_API OverlayMenu* overlay_menu(void);

OVERLAY_API void overlay_menu_set_chrome_painter(OverlayChromePaintFn painter, void* user);
OVERLAY_API OverlayRect overlay_menu_rect(const OverlayMenu* menu);
OVERLAY_API void overlay_menu_set_rect(OverlayMenu* menu, OverlayRect rect);
OVERLAY_API void overlay_menu_set_page(OverlayMenu* menu, int index);
OVERLAY_API int overlay_menu_page_index(const OverlayMenu* menu);
OVERLAY_API size_t overlay_menu_page_count(const OverlayMenu* menu);

OVERLAY_API OverlayPage* overlay_menu_page(OverlayMenu* menu, size_t index);

OVERLAY_API OverlayPage* overlay_page_create(OverlayContext* context, const char* title, const char* nav_section);
OVERLAY_API void overlay_page_destroy(OverlayPage* page);
OVERLAY_API const char* overlay_page_title(const OverlayPage* page);
OVERLAY_API const char* overlay_page_nav_section(const OverlayPage* page);
OVERLAY_API size_t overlay_page_section_count(const OverlayPage* page);
OVERLAY_API OverlaySection* overlay_page_section(OverlayPage* page, size_t index);

OVERLAY_API OverlaySection* overlay_section_create(OverlayPage* page, const char* title);
OVERLAY_API void overlay_section_destroy(OverlaySection* section);
OVERLAY_API const char* overlay_section_title(const OverlaySection* section);
OVERLAY_API size_t overlay_section_widget_count(const OverlaySection* section);
OVERLAY_API OverlayWidget* overlay_section_widget(OverlaySection* section, size_t index);

OVERLAY_API OverlayWidget* overlay_widget_create(OverlaySection* section, OverlayWidgetKind kind, const char* id,
                                                 const char* label);
OVERLAY_API void overlay_widget_destroy(OverlayWidget* widget);

/* Blank mode only: the chrome painter assigns each widget's row rect. */
OVERLAY_API void overlay_widget_set_rect(OverlayWidget* widget, OverlayRect rect);

/* Shifts the widget's row inward by `indent` steps, for nested options. */
OVERLAY_API void overlay_widget_set_indent(OverlayWidget* widget, int indent);

/* ---- widget values -------------------------------------------------- */

OVERLAY_API void overlay_widget_set_bool(OverlayWidget* widget, int value);
OVERLAY_API int overlay_widget_get_bool(const OverlayWidget* widget);

OVERLAY_API void overlay_widget_set_float(OverlayWidget* widget, float value);
OVERLAY_API float overlay_widget_get_float(const OverlayWidget* widget);

OVERLAY_API void overlay_widget_set_int(OverlayWidget* widget, int value);
OVERLAY_API int overlay_widget_get_int(const OverlayWidget* widget);

/* Writes a NUL-terminated copy and returns the required size including the
 * terminator. Call with buffer NULL, capacity 0 to measure. */
OVERLAY_API size_t overlay_widget_get_text(const OverlayWidget* widget, char* buffer, size_t capacity);
OVERLAY_API void overlay_widget_set_text(OverlayWidget* widget, const char* value);

/* 0 = unbound, >0 virtual key, <0 mouse button (-1 left). */
OVERLAY_API void overlay_widget_set_key(OverlayWidget* widget, int code);
OVERLAY_API int overlay_widget_get_key(const OverlayWidget* widget);

OVERLAY_API void overlay_widget_set_color(OverlayWidget* widget, OverlayColor color);
OVERLAY_API OverlayColor overlay_widget_get_color(const OverlayWidget* widget);

OVERLAY_API void overlay_widget_set_range(OverlayWidget* widget, float min, float max);
OVERLAY_API void overlay_widget_set_format(OverlayWidget* widget, const char* format, const char* alt_text);
OVERLAY_API void overlay_widget_set_items(OverlayWidget* widget, const char* const* items, size_t count);
OVERLAY_API void overlay_widget_set_enabled(OverlayWidget* widget, int enabled);

/* ---- painting ------------------------------------------------------- */

OVERLAY_API void overlay_widget_set_painter(OverlayWidget* widget, OverlayPaintFn painter, void* user);

OVERLAY_API OverlayRect overlay_widget_rect(const OverlayWidget* widget);
OVERLAY_API OverlayRect overlay_widget_control_rect(const OverlayWidget* widget, float scale);
OVERLAY_API int overlay_widget_is_hovered(const OverlayWidget* widget);
OVERLAY_API int overlay_widget_is_focused(const OverlayWidget* widget);
OVERLAY_API int overlay_widget_was_clicked(const OverlayWidget* widget);
OVERLAY_API float overlay_widget_anim(const OverlayWidget* widget);

OVERLAY_API float overlay_canvas_scale(const OverlayCanvas* canvas);
OVERLAY_API OverlayVec2 overlay_canvas_measure_text(const OverlayCanvas* canvas, const char* text);
OVERLAY_API OverlayVec2 overlay_canvas_measure_text_sized(const OverlayCanvas* canvas, const char* text, float size);
OVERLAY_API void overlay_canvas_fill_rect(OverlayCanvas* canvas, OverlayRect rect, OverlayColor color, float radius);
OVERLAY_API void overlay_canvas_stroke_rect(OverlayCanvas* canvas, OverlayRect rect, OverlayColor color, float thickness,
                                            float radius);
OVERLAY_API void overlay_canvas_fill_circle(OverlayCanvas* canvas, OverlayVec2 center, float radius, OverlayColor color);
OVERLAY_API void overlay_canvas_stroke_circle(OverlayCanvas* canvas, OverlayVec2 center, float radius, OverlayColor color,
                                              float thickness);
OVERLAY_API void overlay_canvas_fill_triangle(OverlayCanvas* canvas, OverlayVec2 a, OverlayVec2 b, OverlayVec2 c,
                                              OverlayColor color);
OVERLAY_API void overlay_canvas_text(OverlayCanvas* canvas, OverlayVec2 pos, OverlayColor color, const char* text);
OVERLAY_API void overlay_canvas_text_sized(OverlayCanvas* canvas, OverlayVec2 pos, OverlayColor color, float size,
                                           const char* text);
OVERLAY_API void overlay_canvas_text_centered(OverlayCanvas* canvas, OverlayRect rect, OverlayColor color,
                                              const char* text);
OVERLAY_API void overlay_canvas_push_clip(OverlayCanvas* canvas, OverlayRect rect);
OVERLAY_API void overlay_canvas_pop_clip(OverlayCanvas* canvas);

/* ---- config (not implemented until the config phase) ---------------- */

OVERLAY_API OverlayStatus overlay_config_save(const char* name);
OVERLAY_API OverlayStatus overlay_config_load(const char* name);
OVERLAY_API OverlayStatus overlay_config_delete(const char* name);
OVERLAY_API void overlay_config_list(OverlayConfigListFn callback, void* user);

/* ---- notifications -------------------------------------------------- */

typedef enum OverlayToastPosition {
  OVERLAY_TOAST_TOP_RIGHT = 0,
  OVERLAY_TOAST_TOP_LEFT = 1,
  OVERLAY_TOAST_BOTTOM_RIGHT = 2,
  OVERLAY_TOAST_BOTTOM_LEFT = 3
} OverlayToastPosition;

OVERLAY_API void overlay_notify(const char* message);
OVERLAY_API void overlay_event(const char* message);
OVERLAY_API void overlay_toast_position(OverlayToastPosition position);
OVERLAY_API void overlay_toast_duration(float seconds);

/* ---- stylesheet ----------------------------------------------------- */

/*
 * CSS subset: `menu { ... }` sets globals, `class { ... }` a widget class,
 * `#id { ... }` one widget instance (id beats class beats menu).
 * Color properties: accent, track, knob, text, text-dim, panel, border,
 * window-bg, row-hover, nav-selected, button, button-hover, field.
 * Number property: rounding.
 * Values: #rgb, #rrggbb, #rrggbbaa, rgb(r,g,b), rgba(r,g,b,a).
 */
OVERLAY_API OverlayStatus overlay_style_parse(const char* css);
OVERLAY_API OverlayStatus overlay_style_load(const char* utf8_path);
OVERLAY_API void overlay_style_clear(void);

/* ---- scripts -------------------------------------------------------- */

/* Runs one chunk on the host VM (clears script-built menu content first). */
OVERLAY_API OverlayStatus overlay_script_execute(const char* source, const char* chunk_name);

/* Queues a full teardown + reload of the module's scripts folder. */
OVERLAY_API OverlayStatus overlay_script_reload(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OVERLAY_OVERLAY_C_H */
