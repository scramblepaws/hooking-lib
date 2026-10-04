#pragma once

#include <atomic>

namespace overlay {

struct State {
  std::atomic<bool> hooks_installed{false};
  std::atomic<bool> render_ready{false};
  std::atomic<bool> menu_open{false};
  std::atomic<bool> eject_requested{false};
  // Last raw key event for widgets to capture: 0 none, >0 VK code, <0 mouse button.
  std::atomic<int> captured_key{0};
  // Character stream for text widgets; drained by the UI each frame.
  std::atomic<unsigned int> chars[64];
  std::atomic<int> char_count{0};
  // DX12: the Target's direct command queue, captured by the hook so overlay
  // commands are ordered after the Target's own frame on the same queue.
  std::atomic<void*> dx12_queue{nullptr};
};

State& state();

}  // namespace overlay
