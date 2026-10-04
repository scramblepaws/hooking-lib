#pragma once

struct IDXGISwapChain;

namespace overlay::render::dx12 {

bool init(IDXGISwapChain* swapchain, void* hwnd);
void shutdown();
void release_target();
bool create_target(IDXGISwapChain* swapchain);
void new_frame();
void render();

}  // namespace overlay::render::dx12
