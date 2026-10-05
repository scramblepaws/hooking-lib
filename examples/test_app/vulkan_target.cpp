#include <windows.h>
#include <volk.h>

#include <cstdint>
#include <vector>

extern bool g_resize_pending;

namespace {

VkInstance g_instance = VK_NULL_HANDLE;
VkSurfaceKHR g_surface = VK_NULL_HANDLE;
VkPhysicalDevice g_phys = VK_NULL_HANDLE;
VkDevice g_device = VK_NULL_HANDLE;
VkQueue g_queue = VK_NULL_HANDLE;
VkSwapchainKHR g_swapchain = VK_NULL_HANDLE;
VkRenderPass g_render_pass = VK_NULL_HANDLE;
std::vector<VkImage> g_images;
std::vector<VkImageView> g_views;
std::vector<VkFramebuffer> g_framebuffers;
VkFormat g_format = VK_FORMAT_B8G8R8A8_UNORM;
VkExtent2D g_extent = {1280, 720};
uint32_t g_family = 0;
uint32_t g_image_count = 2;
HWND g_hwnd = nullptr;

bool check(VkResult result, const wchar_t* what) {
  if (result == VK_SUCCESS) return true;
  wchar_t message[128] = {};
  wsprintfW(message, L"%s failed (VkResult %d)", what, static_cast<int>(result));
  MessageBoxW(nullptr, message, L"test target", MB_OK | MB_ICONERROR);
  return false;
}

VkResult create_swapchain(VkSwapchainKHR old) {
  VkSurfaceCapabilitiesKHR caps = {};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_phys, g_surface, &caps);
  g_extent = caps.currentExtent;
  if (g_extent.width == 0xFFFFFFFFu) {
    RECT client = {};
    GetClientRect(g_hwnd, &client);
    g_extent = {static_cast<uint32_t>(client.right), static_cast<uint32_t>(client.bottom)};
  }

  VkSwapchainCreateInfoKHR info = {};
  info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  info.surface = g_surface;
  info.minImageCount = caps.minImageCount + 1;
  info.imageFormat = g_format;
  info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  info.imageExtent = g_extent;
  info.imageArrayLayers = 1;
  info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.preTransform = caps.currentTransform;
  info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  info.clipped = VK_TRUE;
  info.oldSwapchain = old;
  return vkCreateSwapchainKHR(g_device, &info, nullptr, &g_swapchain);
}

bool create_swapchain_images() {
  uint32_t count = 0;
  if (!check(vkGetSwapchainImagesKHR(g_device, g_swapchain, &count, nullptr), L"vkGetSwapchainImagesKHR")) return false;
  if (count > 8) count = 8;
  g_image_count = count;
  g_images.resize(count);
  if (!check(vkGetSwapchainImagesKHR(g_device, g_swapchain, &count, g_images.data()), L"vkGetSwapchainImagesKHR"))
    return false;

  g_views.resize(count);
  g_framebuffers.resize(count);
  for (uint32_t i = 0; i < count; ++i) {
    VkImageViewCreateInfo view = {};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = g_images[i];
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = g_format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!check(vkCreateImageView(g_device, &view, nullptr, &g_views[i]), L"vkCreateImageView")) return false;

    VkFramebufferCreateInfo fb = {};
    fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb.renderPass = g_render_pass;
    fb.attachmentCount = 1;
    fb.pAttachments = &g_views[i];
    fb.width = g_extent.width;
    fb.height = g_extent.height;
    fb.layers = 1;
    if (!check(vkCreateFramebuffer(g_device, &fb, nullptr, &g_framebuffers[i]), L"vkCreateFramebuffer")) return false;
  }
  return true;
}

void destroy_swapchain_images() {
  for (VkFramebuffer fb : g_framebuffers)
    if (fb) vkDestroyFramebuffer(g_device, fb, nullptr);
  for (VkImageView view : g_views)
    if (view) vkDestroyImageView(g_device, view, nullptr);
  g_framebuffers.clear();
  g_views.clear();
  g_images.clear();
}

bool create_device_and_swapchain(HWND hwnd) {
  VkWin32SurfaceCreateInfoKHR surface_info = {};
  surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surface_info.hinstance = GetModuleHandleW(nullptr);
  surface_info.hwnd = hwnd;
  if (!check(vkCreateWin32SurfaceKHR(g_instance, &surface_info, nullptr, &g_surface), L"vkCreateWin32SurfaceKHR"))
    return false;

  uint32_t count = 0;
  vkEnumeratePhysicalDevices(g_instance, &count, nullptr);
  if (!count) {
    MessageBoxW(nullptr, L"no Vulkan devices", L"test target", MB_OK | MB_ICONERROR);
    return false;
  }
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(g_instance, &count, devices.data());
  g_phys = devices[0];

  uint32_t family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(g_phys, &family_count, nullptr);
  std::vector<VkQueueFamilyProperties> families(family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(g_phys, &family_count, families.data());
  for (uint32_t i = 0; i < family_count; ++i) {
    VkBool32 present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(g_phys, i, g_surface, &present);
    if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
      g_family = i;
      break;
    }
  }

  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info = {};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.queueFamilyIndex = g_family;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &priority;
  VkDeviceCreateInfo device_info = {};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  device_info.enabledExtensionCount = 1;
  device_info.ppEnabledExtensionNames = device_extensions;
  if (!check(vkCreateDevice(g_phys, &device_info, nullptr, &g_device), L"vkCreateDevice")) return false;
  volkLoadDevice(g_device);
  vkGetDeviceQueue(g_device, g_family, 0, &g_queue);

  VkAttachmentDescription color = {};
  color.format = g_format;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_ref;
  VkSubpassDependency dependency = {};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo pass_info = {};
  pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  pass_info.attachmentCount = 1;
  pass_info.pAttachments = &color;
  pass_info.subpassCount = 1;
  pass_info.pSubpasses = &subpass;
  pass_info.dependencyCount = 1;
  pass_info.pDependencies = &dependency;
  if (!check(vkCreateRenderPass(g_device, &pass_info, nullptr, &g_render_pass), L"vkCreateRenderPass")) return false;

  if (!check(create_swapchain(VK_NULL_HANDLE), L"vkCreateSwapchainKHR")) return false;
  return create_swapchain_images();
}

bool recreate_swapchain() {
  vkDeviceWaitIdle(g_device);
  destroy_swapchain_images();
  VkSwapchainKHR old = g_swapchain;
  g_swapchain = VK_NULL_HANDLE;
  if (create_swapchain(old) != VK_SUCCESS) return false;
  vkDestroySwapchainKHR(g_device, old, nullptr);
  return create_swapchain_images();
}

}  // namespace

int run_vulkan(HWND hwnd) {
  g_hwnd = hwnd;
  if (volkInitialize() != VK_SUCCESS) {
    MessageBoxW(nullptr, L"vulkan-1.dll not available", L"test target", MB_OK | MB_ICONERROR);
    return 1;
  }

  VkApplicationInfo app = {};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "overlay test target";
  app.apiVersion = VK_API_VERSION_1_1;
  const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
  VkInstanceCreateInfo instance_info = {};
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_info.pApplicationInfo = &app;
  instance_info.enabledExtensionCount = 2;
  instance_info.ppEnabledExtensionNames = extensions;
  if (!check(vkCreateInstance(&instance_info, nullptr, &g_instance), L"vkCreateInstance")) return 1;
  volkLoadInstance(g_instance);

  if (!create_device_and_swapchain(hwnd)) return 1;

  VkCommandPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = g_family;
  VkCommandPool pool = VK_NULL_HANDLE;
  check(vkCreateCommandPool(g_device, &pool_info, nullptr, &pool), L"vkCreateCommandPool");
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo alloc = {};
  alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc.commandPool = pool;
  alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc.commandBufferCount = 1;
  check(vkAllocateCommandBuffers(g_device, &alloc, &cmd), L"vkAllocateCommandBuffers");

  VkSemaphoreCreateInfo sem_info = {};
  sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkSemaphore image_available = VK_NULL_HANDLE;
  VkSemaphore render_done = VK_NULL_HANDLE;
  vkCreateSemaphore(g_device, &sem_info, nullptr, &image_available);
  vkCreateSemaphore(g_device, &sem_info, nullptr, &render_done);
  VkFenceCreateInfo fence_info = {};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  VkFence fence = VK_NULL_HANDLE;
  vkCreateFence(g_device, &fence_info, nullptr, &fence);

  float t = 0;
  bool running = true;
  MSG msg = {};
  while (running) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) running = false;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (g_resize_pending) {
      g_resize_pending = false;
      recreate_swapchain();
    }

    vkWaitForFences(g_device, 1, &fence, VK_TRUE, 1000000000ull);
    vkResetFences(g_device, 1, &fence);

    uint32_t index = 0;
    VkResult acquired = vkAcquireNextImageKHR(g_device, g_swapchain, 1000000000ull, image_available, VK_NULL_HANDLE, &index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
      recreate_swapchain();
      continue;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) break;

    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    t += 0.016f;

    VkClearValue clear = {};
    clear.color.float32[0] = 0.10f + 0.03f * static_cast<float>(static_cast<int>(t) % 4);
    clear.color.float32[1] = 0.14f;
    clear.color.float32[2] = 0.20f;
    clear.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo pass = {};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = g_render_pass;
    pass.framebuffer = g_framebuffers[index];
    pass.renderArea.extent = g_extent;
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_done;
    vkQueueSubmit(g_queue, 1, &submit, fence);

    VkPresentInfoKHR present = {};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_done;
    present.swapchainCount = 1;
    present.pSwapchains = &g_swapchain;
    present.pImageIndices = &index;
    const VkResult presented = vkQueuePresentKHR(g_queue, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) recreate_swapchain();
  }

  vkDeviceWaitIdle(g_device);
  destroy_swapchain_images();
  vkDestroySemaphore(g_device, image_available, nullptr);
  vkDestroySemaphore(g_device, render_done, nullptr);
  vkDestroyFence(g_device, fence, nullptr);
  vkDestroyCommandPool(g_device, pool, nullptr);
  vkDestroySwapchainKHR(g_device, g_swapchain, nullptr);
  vkDestroyRenderPass(g_device, g_render_pass, nullptr);
  vkDestroySurfaceKHR(g_instance, g_surface, nullptr);
  vkDestroyDevice(g_device, nullptr);
  vkDestroyInstance(g_instance, nullptr);
  return 0;
}
