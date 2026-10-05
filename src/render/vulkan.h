#pragma once

#include <cstdint>

namespace overlay::render::vulkan {

// Everything the renderer needs, captured from the Target's Vulkan calls.
struct Target {
  void* instance;
  void* physical_device;
  void* device;
  void* queue;
  void* swapchain;
  uint32_t queue_family;
  uint32_t format;
  uint32_t width;
  uint32_t height;
};

bool init(void* hwnd, const Target& target);
bool active();
void new_frame();
void render(void* queue, void* swapchain, uint32_t image_index);
bool create_target(const Target& target);
void release_target(void* swapchain);
void shutdown();

// Uploads an RGBA image and returns a descriptor set usable as an ImTextureID,
// or null when there is no room / the renderer is not ready.
void* create_texture(const void* rgba, uint32_t width, uint32_t height);

}  // namespace overlay::render::vulkan
