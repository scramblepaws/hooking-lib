#pragma once

#include <cstdint>

struct IDXGISwapChain;

namespace overlay::render::dx12 {

bool init(IDXGISwapChain* swapchain, void* hwnd);
bool ready();
void shutdown();
void release_target();
bool create_target(IDXGISwapChain* swapchain);
void new_frame();
void render();

// Uploads an RGBA image and returns a GPU descriptor handle usable as an
// ImTextureID, or null when the overlay descriptor range is full.
void* create_texture(const void* rgba, uint32_t width, uint32_t height);

}  // namespace overlay::render::dx12
