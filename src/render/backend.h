#pragma once

struct IDXGISwapChain;

namespace overlay::render {

// Backend-agnostic seam. The backend (DX11 or DX12) is detected from the
// swapchain's device on first init.
bool init(IDXGISwapChain* swapchain, void* hwnd);
bool ready();
void frame(IDXGISwapChain* swapchain);
void shutdown();
void release_target();
bool create_target(IDXGISwapChain* swapchain);

}  // namespace overlay::render
