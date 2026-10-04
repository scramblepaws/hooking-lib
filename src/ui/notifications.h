#pragma once

#include "ui/canvas.h"

namespace overlay::ui {

enum class ToastPosition {
  TopRight,
  TopLeft,
  BottomRight,
  BottomLeft,
};

void notify(const char* message);
void emit_event(const char* message);
void set_toast_position(ToastPosition position);
void set_toast_duration(float seconds);

// Toasts and the event feed draw regardless of whether the menu is open.
void paint_notifications(Canvas& canvas);

}  // namespace overlay::ui
