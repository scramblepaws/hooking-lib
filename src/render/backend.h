#pragma once

#include "render/vulkan.h"

struct IDXGISwapChain;

namespace overlay::render {

// Backend-agnostic seam. The backend (DX11, DX12 or Vulkan) is detected from
// the swapchain's device on first init.
bool init(IDXGISwapChain* swapchain, void* hwnd);
bool init_vulkan(void* hwnd, const vulkan::Target& target);
bool ready();
void frame(IDXGISwapChain* swapchain);
void frame_vulkan(void* queue, void* swapchain, uint32_t image_index);
void shutdown();
// Fault path: stop rendering without touching renderer state (it may be
// inconsistent after a contained crash). Resources leak until process exit.
void abandon();
void release_target();
bool create_target(IDXGISwapChain* swapchain);

}  // namespace overlay::render
