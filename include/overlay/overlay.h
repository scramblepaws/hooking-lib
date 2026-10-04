#pragma once

namespace overlay {

enum class Status {
  ok,
  already_running,
  not_running,
  hook_failed,
};

// Installs the swapchain hooks. Rendering starts at the Target's next Present.
Status init();

// Requests teardown; actual shutdown happens on the render thread, then the
// module ejects itself.
Status shutdown();

// True once the renderer has initialized on a Target swapchain.
bool running();

}  // namespace overlay
