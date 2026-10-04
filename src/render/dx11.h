#pragma once

struct IDXGISwapChain;
struct ID3D11Device;

namespace overlay::render::dx11 {

bool init(IDXGISwapChain* swapchain, void* hwnd);
void shutdown();
void release_target();
bool create_target(IDXGISwapChain* swapchain);
void new_frame();
void render();

// Null when the DX11 backend is not active (used by the texture loader).
ID3D11Device* device();

}  // namespace overlay::render::dx11
