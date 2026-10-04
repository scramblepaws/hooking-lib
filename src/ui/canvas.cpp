#include "ui/canvas.h"

#include "imgui.h"

namespace overlay::ui {

namespace {

ImVec2 to_im(const Vec2& v) { return ImVec2(v.x, v.y); }
ImVec2 im_max(const Rect& r) { return ImVec2(r.x + r.w, r.y + r.h); }
ImU32 to_u32(const Color& c) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a)); }

class ImCanvas : public Canvas {
 public:
  ImCanvas(ImDrawList* draw_list, float scale) : draw_list_(draw_list), scale_(scale) {}

  float scale() const override { return scale_; }

  Vec2 measure_text(const char* text) const override {
    const ImVec2 size = ImGui::CalcTextSize(text);
    return {size.x, size.y};
  }

  Vec2 measure_text_sized(const char* text, float size) const override {
    const ImVec2 measured = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    return {measured.x, measured.y};
  }

  void fill_rect(const Rect& rect, Color color, float radius) override {
    draw_list_->AddRectFilled(to_im({rect.x, rect.y}), im_max(rect), to_u32(color), radius);
  }

  void stroke_rect(const Rect& rect, Color color, float thickness, float radius) override {
    draw_list_->AddRect(to_im({rect.x, rect.y}), im_max(rect), to_u32(color), radius, ImDrawFlags_None, thickness);
  }

  void fill_circle(Vec2 center, float radius, Color color) override {
    draw_list_->AddCircleFilled(to_im(center), radius, to_u32(color));
  }

  void stroke_circle(Vec2 center, float radius, Color color, float thickness) override {
    draw_list_->AddCircle(to_im(center), radius, to_u32(color), 0, thickness);
  }

  void fill_triangle(Vec2 a, Vec2 b, Vec2 c, Color color) override {
    draw_list_->AddTriangleFilled(to_im(a), to_im(b), to_im(c), to_u32(color));
  }

  void text(Vec2 pos, Color color, const char* text) override {
    draw_list_->AddText(to_im(pos), to_u32(color), text);
  }

  void text_sized(Vec2 pos, Color color, float size, const char* text) override {
    draw_list_->AddText(ImGui::GetFont(), size, to_im(pos), to_u32(color), text);
  }

  void text_centered(const Rect& rect, Color color, const char* content) override {
    const Vec2 size = measure_text(content);
    text({rect.x + (rect.w - size.x) * 0.5f, rect.y + (rect.h - size.y) * 0.5f}, color, content);
  }

  void push_clip(const Rect& rect) override {
    draw_list_->PushClipRect(to_im({rect.x, rect.y}), im_max(rect), true);
  }

  void pop_clip() override { draw_list_->PopClipRect(); }

 private:
  ImDrawList* draw_list_;
  float scale_;
};

}  // namespace

std::unique_ptr<Canvas> make_canvas(void* draw_list, float scale) {
  return std::make_unique<ImCanvas>(static_cast<ImDrawList*>(draw_list), scale);
}

}  // namespace overlay::ui
