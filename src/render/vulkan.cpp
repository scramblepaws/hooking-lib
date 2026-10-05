#include "render/vulkan.h"

#include <volk.h>

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

bool g_ready = false;
bool g_imgui_ready = false;

// Swapchain rebuilds (vkCreateSwapchainKHR from the Target's thread) race the
// present-time render (present thread); everything touches the same resources.
std::mutex g_lock;

void check_result(VkResult result) {
  if (result != VK_SUCCESS) log("vulkan: error %d", static_cast<int>(result));
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
  info.DescriptorPoolSize = 8;
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

void shutdown() {
  std::lock_guard<std::mutex> lock(g_lock);
  if (!g_device) return;
  g_ready = false;
  vkDeviceWaitIdle(g_device);
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
