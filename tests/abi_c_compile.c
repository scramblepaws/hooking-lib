#include "overlay/overlay_c.h"

#include <stddef.h>

static void sample_painter(void* user, const OverlayWidget* widget, OverlayCanvas* canvas) {
  const float scale = overlay_canvas_scale(canvas);
  const OverlayRect control = overlay_widget_control_rect(widget, scale);
  (void)user;
  overlay_canvas_fill_rect(canvas, control, (OverlayColor){0.70f, 0.21f, 0.21f, 1.0f}, 3.0f * scale);
  overlay_canvas_text_centered(canvas, control, (OverlayColor){1.0f, 1.0f, 1.0f, 1.0f}, "custom");
  overlay_canvas_push_clip(canvas, control);
  overlay_canvas_pop_clip(canvas);
}

int main(void) {
  OverlayInitInfo info;
  info.size = (uint32_t)sizeof(info);
  info.backend = OVERLAY_BACKEND_AUTO;
  info.flags = 0;
  info.target_hwnd = NULL;
  info.menu_title = "sample";

  if (overlay_abi_version() != OVERLAY_ABI_VERSION) return 1;

  (void)overlay_init(&info);
  (void)overlay_is_ready();
  (void)overlay_set_menu_open(1);
  (void)overlay_is_menu_open();

  OverlayContext* context = overlay_context();
  OverlayPage* page = overlay_page_create(context, "Aimbot", "Legit");
  OverlaySection* section = overlay_section_create(page, "MAIN");
  OverlayWidget* widget = overlay_widget_create(section, OVERLAY_WIDGET_SLIDER, "fov", "Field of View");
  overlay_widget_set_range(widget, 0.0f, 180.0f);
  overlay_widget_set_format(widget, "%.0f", NULL);
  overlay_widget_set_float(widget, 90.0f);
  overlay_widget_set_painter(widget, sample_painter, NULL);

  char buffer[64];
  const size_t needed = overlay_widget_get_text(widget, buffer, sizeof(buffer));
  if (needed > sizeof(buffer)) buffer[0] = '\0';

  (void)overlay_config_save("default");
  (void)overlay_config_load("default");
  overlay_notify("ready");
  overlay_event("injected");

  overlay_widget_destroy(widget);
  overlay_section_destroy(section);
  overlay_page_destroy(page);
  (void)overlay_shutdown();
  return 0;
}
