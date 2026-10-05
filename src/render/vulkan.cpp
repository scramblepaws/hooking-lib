#include "render/vulkan.h"

#include <volk.h>

#include <cstring>
#include <mutex>
#include <vector>

#include "core/log.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_win32.h"

namespace overlay::render::vulkan {

namespace {

VkInstance g_instance = VK_NULL_HANDLE;
VkPhysicalDevice g_physical = VK_NULL_HANDLE;
VkDevice g_device = VK_NULL_HANDLE;
VkQueue g_queue = VK_NULL_HANDLE;
uint32_t g_family = 0;
void* g_hwnd = nullptr;

VkRenderPass g_pass = VK_NULL_HANDLE;
VkCommandPool g_pool = VK_NULL_HANDLE;
VkCommandBuffer g_cmd = VK_NULL_HANDLE;
VkFence g_fence = VK_NULL_HANDLE;
bool g_fence_pending = false;

VkSwapchainKHR g_swapchain = VK_NULL_HANDLE;
VkFormat g_format = VK_FORMAT_UNDEFINED;
VkExtent2D g_extent = {0, 0};
std::vector<VkImage> g_images;
std::vector<VkImageView> g_views;
std::vector<VkFramebuffer> g_fbs;

constexpr uint32_t kMaxOverlayTextures = 48;  // leaves headroom in the 64-slot pool
std::vector<VkImage> g_texture_images;
std::vector<VkImageView> g_texture_views;
std::vector<VkDeviceMemory> g_texture_memory;
uint32_t g_texture_count = 0;

bool g_ready = false;
bool g_imgui_ready = false;

// Swapchain rebuilds (vkCreateSwapchainKHR from the Target's thread) race the
// present-time render (present thread); everything touches the same resources.
std::mutex g_lock;

void check_result(VkResult result) {
  if (result != VK_SUCCESS) log("vulkan: error %d", static_cast<int>(result));
}

uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memory = {};
  vkGetPhysicalDeviceMemoryProperties(g_physical, &memory);
  for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
    if ((type_bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & properties) == properties) return i;
  return UINT32_MAX;
}

bool create_render_pass(VkFormat format) {
  VkAttachmentDescription color = {};
  color.format = format;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // keep the Target's frame
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &ref;
  VkSubpassDependency dependency = {};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.attachmentCount = 1;
  info.pAttachments = &color;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = 1;
  info.pDependencies = &dependency;
  return vkCreateRenderPass(g_device, &info, nullptr, &g_pass) == VK_SUCCESS;
}

bool init_imgui_backend(uint32_t image_count) {
  ImGui_ImplVulkan_InitInfo info = {};
  info.ApiVersion = VK_API_VERSION_1_1;
  info.Instance = g_instance;
  info.PhysicalDevice = g_physical;
  info.Device = g_device;
  info.QueueFamily = g_family;
  info.Queue = g_queue;
  info.DescriptorPoolSize = 64;  // room for the font atlas plus overlay images
  info.MinImageCount = image_count < 2 ? 2 : image_count;
  info.ImageCount = info.MinImageCount;
  info.PipelineInfoMain.RenderPass = g_pass;
  info.PipelineInfoMain.Subpass = 0;
  info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  info.CheckVkResultFn = check_result;
  if (!ImGui_ImplVulkan_Init(&info)) return false;
  g_imgui_ready = true;
  return true;
}

void destroy_swapchain_resources() {
  for (VkFramebuffer fb : g_fbs)
    if (fb) vkDestroyFramebuffer(g_device, fb, nullptr);
  for (VkImageView view : g_views)
    if (view) vkDestroyImageView(g_device, view, nullptr);
  g_fbs.clear();
  g_views.clear();
  g_images.clear();
  g_swapchain = VK_NULL_HANDLE;
}

}  // namespace

bool init(void* hwnd, const Target& target) {
  if (g_ready) {
    if (target.device == g_device) return true;
    shutdown();
  }
  if (volkInitialize() != VK_SUCCESS) {
    log("vulkan: no loader");
    return false;
  }
  g_instance = static_cast<VkInstance>(target.instance);
  g_physical = static_cast<VkPhysicalDevice>(target.physical_device);
  g_device = static_cast<VkDevice>(target.device);
  g_queue = static_cast<VkQueue>(target.queue);
  g_family = target.queue_family;
  g_hwnd = hwnd;
  volkLoadInstance(g_instance);
  volkLoadDevice(g_device);
  if (!vkGetSwapchainImagesKHR || !vkCreateImageView || !vkCreateFramebuffer) {
    log("vulkan: swapchain entry points unavailable");
    g_device = VK_NULL_HANDLE;
    return false;
  }

  VkCommandPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = g_family;
  if (vkCreateCommandPool(g_device, &pool_info, nullptr, &g_pool) != VK_SUCCESS) {
    log("vulkan: command pool failed");
    g_device = VK_NULL_HANDLE;
    return false;
  }
  VkCommandBufferAllocateInfo alloc = {};
  alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc.commandPool = g_pool;
  alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc.commandBufferCount = 1;
  if (vkAllocateCommandBuffers(g_device, &alloc, &g_cmd) != VK_SUCCESS) {
    log("vulkan: command buffer failed");
    shutdown();
    return false;
  }
  VkFenceCreateInfo fence_info = {};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  if (vkCreateFence(g_device, &fence_info, nullptr, &g_fence) != VK_SUCCESS) {
    log("vulkan: fence failed");
    shutdown();
    return false;
  }
  if (!create_render_pass(static_cast<VkFormat>(target.format))) {
    log("vulkan: render pass failed");
    shutdown();
    return false;
  }
  g_format = static_cast<VkFormat>(target.format);
  g_ready = true;

  if (!create_target(target)) {
    log("vulkan: target init failed");
    shutdown();
    return false;
  }
  log("vulkan: renderer ready (format=%u %ux%u)", target.format, target.width, target.height);
  return true;
}

bool active() { return g_ready; }

void new_frame() {
  ImGui_ImplWin32_NewFrame();
  ImGui_ImplVulkan_NewFrame();
  ImGui::NewFrame();
}

bool create_target(const Target& target) {
  std::lock_guard<std::mutex> lock(g_lock);
  if (!g_ready || target.device != g_device) return false;
  vkDeviceWaitIdle(g_device);
  destroy_swapchain_resources();

  g_swapchain = static_cast<VkSwapchainKHR>(target.swapchain);
  g_extent = {target.width, target.height};

  const VkFormat format = static_cast<VkFormat>(target.format);
  if (format != g_format) {
    if (g_imgui_ready) {
      ImGui_ImplVulkan_Shutdown();
      g_imgui_ready = false;
    }
    if (g_pass) vkDestroyRenderPass(g_device, g_pass, nullptr);
    g_pass = VK_NULL_HANDLE;
    if (!create_render_pass(format)) return false;
    g_format = format;
  }

  uint32_t count = 0;
  if (vkGetSwapchainImagesKHR(g_device, g_swapchain, &count, nullptr) != VK_SUCCESS || count == 0) {
    log("vulkan: swapchain images unavailable");
    return false;
  }
  g_images.resize(count);
  if (vkGetSwapchainImagesKHR(g_device, g_swapchain, &count, g_images.data()) != VK_SUCCESS) return false;

  g_views.resize(count);
  g_fbs.resize(count);
  for (uint32_t i = 0; i < count; ++i) {
    VkImageViewCreateInfo view = {};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = g_images[i];
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = g_format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(g_device, &view, nullptr, &g_views[i]) != VK_SUCCESS) return false;

    VkFramebufferCreateInfo fb = {};
    fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb.renderPass = g_pass;
    fb.attachmentCount = 1;
    fb.pAttachments = &g_views[i];
    fb.width = g_extent.width;
    fb.height = g_extent.height;
    fb.layers = 1;
    if (vkCreateFramebuffer(g_device, &fb, nullptr, &g_fbs[i]) != VK_SUCCESS) return false;
  }
  if (!g_imgui_ready) {
    if (!init_imgui_backend(count)) return false;
  } else {
    ImGui_ImplVulkan_SetMinImageCount(count < 2 ? 2 : count);
  }
  return true;
}

void release_target(void* swapchain) {
  std::lock_guard<std::mutex> lock(g_lock);
  if (!g_ready || static_cast<VkSwapchainKHR>(swapchain) != g_swapchain) return;
  vkDeviceWaitIdle(g_device);
  destroy_swapchain_resources();
}

void render(void* queue, void* swapchain, uint32_t image_index) {
  std::lock_guard<std::mutex> lock(g_lock);
  if (!g_ready || !g_imgui_ready) return;
  if (static_cast<VkSwapchainKHR>(swapchain) != g_swapchain || image_index >= g_fbs.size() || !g_fbs[image_index])
    return;

  ImGui::Render();
  ImDrawData* draw = ImGui::GetDrawData();
  if (!draw) return;

  // Wait for the previous frame before recording, so a timed-out submission
  // can never be resubmitted with a fence that is still in use.
  if (g_fence_pending) {
    if (vkWaitForFences(g_device, 1, &g_fence, VK_TRUE, 1000000000ull) != VK_SUCCESS) {
      static bool logged = false;
      if (!logged) {
        logged = true;
        log("vulkan: fence wait timed out; skipping rendering");
      }
      return;
    }
    vkResetFences(g_device, 1, &g_fence);
    g_fence_pending = false;
  }

  vkResetCommandBuffer(g_cmd, 0);
  VkCommandBufferBeginInfo begin = {};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(g_cmd, &begin) != VK_SUCCESS) return;

  // The Target's last use left the image at PRESENT_SRC (standard engine
  // behavior); take it to color attachment, draw, and let the render pass
  // return it to PRESENT_SRC.
  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = g_images[image_index];
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(g_cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

  VkRenderPassBeginInfo pass = {};
  pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  pass.renderPass = g_pass;
  pass.framebuffer = g_fbs[image_index];
  pass.renderArea.extent = g_extent;
  vkCmdBeginRenderPass(g_cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
  ImGui_ImplVulkan_RenderDrawData(draw, g_cmd);
  vkCmdEndRenderPass(g_cmd);
  if (vkEndCommandBuffer(g_cmd) != VK_SUCCESS) return;

  VkSubmitInfo submit = {};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &g_cmd;
  const VkResult submitted = vkQueueSubmit(static_cast<VkQueue>(queue), 1, &submit, g_fence);
  if (submitted == VK_SUCCESS)
    g_fence_pending = true;
  else
    check_result(submitted);  // fence stays unsignaled and reusable
}

void release_texture_resources() {
  for (VkImageView view : g_texture_views)
    if (view) vkDestroyImageView(g_device, view, nullptr);
  for (VkImage image : g_texture_images)
    if (image) vkDestroyImage(g_device, image, nullptr);
  for (VkDeviceMemory memory : g_texture_memory)
    if (memory) vkFreeMemory(g_device, memory, nullptr);
  g_texture_views.clear();
  g_texture_images.clear();
  g_texture_memory.clear();
  g_texture_count = 0;
}

void* create_texture(const void* rgba, uint32_t width, uint32_t height) {
  std::lock_guard<std::mutex> lock(g_lock);
  if (!g_ready || !g_imgui_ready || !rgba || width == 0 || height == 0) return nullptr;
  if (g_texture_count >= kMaxOverlayTextures) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      log("vulkan: overlay texture limit reached (%u)", kMaxOverlayTextures);
    }
    return nullptr;
  }

  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory image_memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory staging_memory = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4;

  auto fail = [&]() -> void* {
    if (cmd) vkFreeCommandBuffers(g_device, g_pool, 1, &cmd);
    if (staging) vkDestroyBuffer(g_device, staging, nullptr);
    if (staging_memory) vkFreeMemory(g_device, staging_memory, nullptr);
    if (view) vkDestroyImageView(g_device, view, nullptr);
    if (image) vkDestroyImage(g_device, image, nullptr);
    if (image_memory) vkFreeMemory(g_device, image_memory, nullptr);
    return nullptr;
  };

  VkImageCreateInfo image_info = {};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  image_info.extent = {width, height, 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(g_device, &image_info, nullptr, &image) != VK_SUCCESS) return fail();

  VkMemoryRequirements requirements = {};
  vkGetImageMemoryRequirements(g_device, image, &requirements);
  VkMemoryAllocateInfo image_alloc = {};
  image_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  image_alloc.allocationSize = requirements.size;
  image_alloc.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (image_alloc.memoryTypeIndex == UINT32_MAX ||
      vkAllocateMemory(g_device, &image_alloc, nullptr, &image_memory) != VK_SUCCESS ||
      vkBindImageMemory(g_device, image, image_memory, 0) != VK_SUCCESS)
    return fail();

  VkBufferCreateInfo buffer_info = {};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = bytes;
  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(g_device, &buffer_info, nullptr, &staging) != VK_SUCCESS) return fail();
  vkGetBufferMemoryRequirements(g_device, staging, &requirements);
  VkMemoryAllocateInfo staging_alloc = {};
  staging_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  staging_alloc.allocationSize = requirements.size;
  staging_alloc.memoryTypeIndex = find_memory_type(
      requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (staging_alloc.memoryTypeIndex == UINT32_MAX ||
      vkAllocateMemory(g_device, &staging_alloc, nullptr, &staging_memory) != VK_SUCCESS ||
      vkBindBufferMemory(g_device, staging, staging_memory, 0) != VK_SUCCESS)
    return fail();
  void* mapped = nullptr;
  if (vkMapMemory(g_device, staging_memory, 0, bytes, 0, &mapped) != VK_SUCCESS) return fail();
  std::memcpy(mapped, rgba, static_cast<size_t>(bytes));
  vkUnmapMemory(g_device, staging_memory);

  VkCommandBufferAllocateInfo cmd_alloc = {};
  cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmd_alloc.commandPool = g_pool;
  cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmd_alloc.commandBufferCount = 1;
  if (vkAllocateCommandBuffers(g_device, &cmd_alloc, &cmd) != VK_SUCCESS) return fail();

  VkCommandBufferBeginInfo begin = {};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &begin);
  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
  VkBufferImageCopy region = {};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {width, height, 1};
  vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
  if (vkEndCommandBuffer(cmd) != VK_SUCCESS) return fail();

  VkSubmitInfo submit = {};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  if (vkQueueSubmit(g_queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS) return fail();
  vkQueueWaitIdle(g_queue);
  vkFreeCommandBuffers(g_device, g_pool, 1, &cmd);
  cmd = VK_NULL_HANDLE;
  vkDestroyBuffer(g_device, staging, nullptr);
  vkFreeMemory(g_device, staging_memory, nullptr);
  staging = VK_NULL_HANDLE;
  staging_memory = VK_NULL_HANDLE;

  VkImageViewCreateInfo view_info = {};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.image = image;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (vkCreateImageView(g_device, &view_info, nullptr, &view) != VK_SUCCESS) return fail();

  const VkDescriptorSet set =
      ImGui_ImplVulkan_AddTexture(view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  if (set == VK_NULL_HANDLE) return fail();

  g_texture_images.push_back(image);
  g_texture_views.push_back(view);
  g_texture_memory.push_back(image_memory);
  ++g_texture_count;
  return reinterpret_cast<void*>(set);
}

void shutdown() {
  std::lock_guard<std::mutex> lock(g_lock);
  if (!g_device) return;
  g_ready = false;
  vkDeviceWaitIdle(g_device);
  release_texture_resources();
  if (g_imgui_ready) {
    ImGui_ImplVulkan_Shutdown();
    g_imgui_ready = false;
  }
  destroy_swapchain_resources();
  if (g_fence) vkDestroyFence(g_device, g_fence, nullptr);
  if (g_cmd && g_pool) vkFreeCommandBuffers(g_device, g_pool, 1, &g_cmd);
  if (g_pool) vkDestroyCommandPool(g_device, g_pool, nullptr);
  if (g_pass) vkDestroyRenderPass(g_device, g_pass, nullptr);
  g_fence = VK_NULL_HANDLE;
  g_fence_pending = false;
  g_cmd = VK_NULL_HANDLE;
  g_pool = VK_NULL_HANDLE;
  g_pass = VK_NULL_HANDLE;
  g_device = VK_NULL_HANDLE;
  g_queue = VK_NULL_HANDLE;
  g_instance = VK_NULL_HANDLE;
  g_physical = VK_NULL_HANDLE;
  g_format = VK_FORMAT_UNDEFINED;
  g_extent = {0, 0};
  log("vulkan shutdown");
}

}  // namespace overlay::render::vulkan
