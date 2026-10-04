#pragma once

namespace overlay::ui {

// scale = DPI scale (1.0 = 96 DPI). All geometry follows it at runtime.
void init(float scale);
void set_scale(float scale);
void draw();

// Toasts and the event feed; drawn whether or not the menu is open.
void draw_overlays();

// Saves the menu's current values under the active config name (autosave on eject).
void save_active_config();

}  // namespace overlay::ui
