#pragma once

#include <memory>

namespace overlay::ui {

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;
};

struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;

  float right() const { return x + w; }
  float bottom() const { return y + h; }
  bool contains(Vec2 p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
};

struct Color {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;
};

// The backend-agnostic drawing surface a Painter draws into.
class Canvas {
 public:
  virtual ~Canvas() = default;

  virtual float scale() const = 0;
  virtual Vec2 measure_text(const char* text) const = 0;
  virtual Vec2 measure_text_sized(const char* text, float size) const = 0;
  virtual void fill_rect(const Rect& rect, Color color, float radius = 0.0f) = 0;
  virtual void stroke_rect(const Rect& rect, Color color, float thickness = 1.0f, float radius = 0.0f) = 0;
  virtual void fill_circle(Vec2 center, float radius, Color color) = 0;
  virtual void stroke_circle(Vec2 center, float radius, Color color, float thickness = 1.0f) = 0;
  virtual void stroke_line(Vec2 a, Vec2 b, Color color, float thickness = 1.0f) = 0;
  virtual void fill_triangle(Vec2 a, Vec2 b, Vec2 c, Color color) = 0;
  virtual void text(Vec2 pos, Color color, const char* text) = 0;
  virtual void text_sized(Vec2 pos, Color color, float size, const char* text) = 0;
  virtual void text_centered(const Rect& rect, Color color, const char* text) = 0;
  virtual void push_clip(const Rect& rect) = 0;
  virtual void pop_clip() = 0;
};

// draw_list is an ImDrawList* today; kept void* so Canvas stays ImGui-free.
std::unique_ptr<Canvas> make_canvas(void* draw_list, float scale);

}  // namespace overlay::ui
