#pragma once

#include "ui/canvas.h"

namespace overlay::ui {

inline constexpr Color kWindowBg{0.074f, 0.074f, 0.080f, 1.0f};
inline constexpr Color kPanelBg{0.090f, 0.090f, 0.102f, 1.0f};
inline constexpr Color kAccent{0.70f, 0.21f, 0.21f, 1.0f};
inline constexpr Color kTextBright{0.91f, 0.91f, 0.91f, 1.0f};
inline constexpr Color kTextDim{0.55f, 0.55f, 0.57f, 1.0f};
inline constexpr Color kBorder{0.164f, 0.164f, 0.180f, 1.0f};
inline constexpr Color kTrack{0.149f, 0.149f, 0.165f, 1.0f};
inline constexpr Color kKnob{0.91f, 0.91f, 0.91f, 1.0f};
inline constexpr Color kRowHover{0.118f, 0.118f, 0.133f, 1.0f};
inline constexpr Color kNavSelected{0.125f, 0.125f, 0.145f, 1.0f};
inline constexpr Color kButtonBg{0.102f, 0.102f, 0.114f, 1.0f};
inline constexpr Color kButtonHover{0.129f, 0.129f, 0.141f, 1.0f};

inline Color lerp(const Color& a, const Color& b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

}  // namespace overlay::ui
