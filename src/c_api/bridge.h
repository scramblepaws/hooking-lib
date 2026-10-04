#pragma once

#include "ui/canvas.h"

// Handle layout shared between the UI internals and the C ABI. Handles for
// pages, sections and widgets are their internal objects reinterpreted; only
// the canvas needs a wrapper because it carries a per-frame implementation.
struct OverlayCanvas {
  overlay::ui::Canvas* impl = nullptr;
};
