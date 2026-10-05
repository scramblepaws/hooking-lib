#include "ui/notifications.h"

#include <algorithm>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "imgui.h"

#include "ui/theme.h"

namespace overlay::ui {

namespace {

struct Message {
  std::string text;
  double start = 0.0;
};

std::mutex g_mutex;
std::deque<std::string> g_pending_toasts;
std::deque<std::string> g_pending_events;
std::vector<Message> g_toasts;
std::vector<Message> g_events;
ToastPosition g_position = ToastPosition::TopRight;
float g_toast_duration = 4.0f;

constexpr float kEventDuration = 5.0f;
constexpr size_t kMaxToasts = 4;
constexpr size_t kMaxEvents = 3;
constexpr float kFadeIn = 0.15f;
constexpr float kFadeOut = 0.6f;

float alpha_at(double now, double start, float duration) {
  const float age = static_cast<float>(now - start);
  if (age < 0.0f) return 0.0f;
  if (age < kFadeIn) return age / kFadeIn;
  const float remaining = duration - age;
  if (remaining <= 0.0f) return -1.0f;
  if (remaining < kFadeOut) return remaining / kFadeOut;
  return 1.0f;
}

}  // namespace

void notify(const char* message) {
  if (!message || !*message) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_pending_toasts.emplace_back(message);
  while (g_pending_toasts.size() > 16) g_pending_toasts.pop_front();
}

void emit_event(const char* message) {
  if (!message || !*message) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_pending_events.emplace_back(message);
  while (g_pending_events.size() > 16) g_pending_events.pop_front();
}

void set_toast_position(ToastPosition position) { g_position = position; }
void set_toast_duration(float seconds) { g_toast_duration = seconds < 1.0f ? 1.0f : seconds; }

void paint_notifications(Canvas& canvas) {
  const double now = ImGui::GetTime();
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float scale = canvas.scale();

  std::deque<std::string> incoming_toasts;
  std::deque<std::string> incoming_events;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    incoming_toasts.swap(g_pending_toasts);
    incoming_events.swap(g_pending_events);
  }
  for (const std::string& text : incoming_toasts) g_toasts.push_back({text, now});
  for (const std::string& text : incoming_events) g_events.push_back({text, now});
  while (g_toasts.size() > kMaxToasts) g_toasts.erase(g_toasts.begin());
  while (g_events.size() > kMaxEvents) g_events.erase(g_events.begin());

  g_toasts.erase(std::remove_if(g_toasts.begin(), g_toasts.end(),
                                [&](const Message& message) { return alpha_at(now, message.start, g_toast_duration) < 0.0f; }),
                 g_toasts.end());
  g_events.erase(std::remove_if(g_events.begin(), g_events.end(),
                                [&](const Message& message) { return alpha_at(now, message.start, kEventDuration) < 0.0f; }),
                 g_events.end());

  const Color panel = theme_color("panel", theme().panel);
  const Color border = theme_color("border", theme().border);
  const Color text_color = theme_color("text", theme().text);
  const Color accent = theme_color("accent", theme().accent);
  const float padding = 14.0f * scale;
  const float toast_height = 34.0f * scale;
  const float gap = 8.0f * scale;
  const float margin = 16.0f * scale;

  const bool top = g_position == ToastPosition::TopRight || g_position == ToastPosition::TopLeft;
  const bool right = g_position == ToastPosition::TopRight || g_position == ToastPosition::BottomRight;

  float y = top ? viewport->WorkPos.y + margin
                : viewport->WorkPos.y + viewport->WorkSize.y - margin - toast_height;
  for (auto it = g_toasts.rbegin(); it != g_toasts.rend(); ++it) {
    const float alpha = alpha_at(now, it->start, g_toast_duration);
    if (alpha <= 0.0f) continue;
    const Vec2 size = canvas.measure_text(it->text.c_str());
    const float width = (size.x + padding * 2.0f + 8.0f * scale) < 220.0f * scale
                            ? 220.0f * scale
                            : size.x + padding * 2.0f + 8.0f * scale;
    const float x = right ? viewport->WorkPos.x + viewport->WorkSize.x - margin - width : viewport->WorkPos.x + margin;

    Color background = panel;
    background.a *= alpha;
    Color accent_bar = accent;
    accent_bar.a *= alpha;
    Color label = text_color;
    label.a *= alpha;

    const Rect box{x, y, width, toast_height};
    canvas.fill_rect(box, background, 0.0f);
    canvas.stroke_rect(box, border, 1.0f, 0.0f);
    canvas.fill_rect({box.x, box.y, 3.0f * scale, box.h}, accent_bar, 0.0f);
    canvas.text({box.x + padding, box.y + (box.h - size.y) * 0.5f}, label, it->text.c_str());

    if (top)
      y += toast_height + gap;
    else
      y -= toast_height + gap;
  }

  float event_y = viewport->WorkPos.y + viewport->WorkSize.y - margin - 26.0f * scale;
  for (auto it = g_events.rbegin(); it != g_events.rend(); ++it) {
    const float alpha = alpha_at(now, it->start, kEventDuration);
    if (alpha <= 0.0f) continue;
    const Vec2 size = canvas.measure_text(it->text.c_str());
    const float width = size.x + padding * 2.0f + 8.0f * scale;
    const Rect box{viewport->WorkPos.x + (viewport->WorkSize.x - width) * 0.5f, event_y, width, 26.0f * scale};

    Color background = panel;
    background.a *= alpha;
    Color dot = accent;
    dot.a *= alpha;
    Color label = text_color;
    label.a *= alpha;

    canvas.fill_rect(box, background, 0.0f);
    canvas.stroke_rect(box, border, 1.0f, 0.0f);
    canvas.fill_circle({box.x + padding * 0.6f, box.y + box.h * 0.5f}, 3.0f * scale, dot);
    canvas.text({box.x + padding + 4.0f * scale, box.y + (box.h - size.y) * 0.5f}, label, it->text.c_str());
    event_y -= box.h + 6.0f * scale;
  }
}

}  // namespace overlay::ui
