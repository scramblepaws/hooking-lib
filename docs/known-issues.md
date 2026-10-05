# Known issues

Every known limit of the current release, with what you will actually see and
what to do about it. If you hit something not on this list, capture
`overlay.log` from next to the DLL and the steps to reproduce.

## DX12 and Vulkan targets need early injection

- **You see:** the injector reports success, but no overlay ever appears.
  The log has no `render ready`; DX12 may log `dx12: waiting for the target
  queue`.
- **Why:** capture works by hooking the device/queue creation path, so the
  DLL must be loaded before the Target creates its device. Mid-run injection
  into an established device has nothing stable left to hook.
- **Do:** `injector --launch <exe> [args...]`, or link the library into the
  host (host mode, the DllMain handshake).

## Vulkan eject leaves the module loaded

- **You see:** `End` stops rendering, the log says the module stays loaded,
  and injecting again reports the overlay is already attached.
- **Why:** the Target has cached pointers into the module from the wrapped
  `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr`. Unloading would crash it.
- **Do:** restart the Target to inject again. DX11/DX12 targets unload
  normally.

## Vulkan assumes the presented image is in PRESENT_SRC

- **You see:** the overlay flickers, appears for one frame, or is missing on
  some Vulkan titles.
- **Why:** at `vkQueuePresentKHR` time the presented image is transitioned
  from `PRESENT_SRC_KHR` to color-attachment and back. Engines that leave the
  image in another layout break that assumption.
- **Do:** report the title with `overlay.log`; there is no workaround.

## DX12 assumes the backbuffer is in PRESENT state

- **You see:** occasional flicker or a stale frame on engines that submit
  from several queues without a present-state barrier.
- **Why:** the overlay transitions `PRESENT` to render target and back around
  its own drawing; a fully racy multi-queue engine can run between those
  points.
- **Do:** report the title; there is no workaround.

## Overlay image limits per session

- **You see:** after many distinct `Drawing` images, new ones stop appearing.
  The log shows `overlay texture descriptors exhausted (128)` on DX12 or
  `overlay texture limit reached (48)` on Vulkan, once.
- **Why:** each distinct image takes a fixed descriptor slot that lives until
  process exit; the cache never evicts.
- **Do:** reuse image paths; restart the Target to reset the slots.

## Multi-swapchain titles get one overlaid swapchain

- **You see:** the overlay appears in one window/swapchain only.
- **Why:** the renderer tracks a single presenting swapchain (DX12: the
  captured present queue's; Vulkan: the most recently created).
- **Do:** none; report the title if it matters.

## Protected or anti-cheat processes

- **You see:** injection fails (`OpenProcess` denied, remote thread blocked)
  or the Target exits immediately after injection.
- **Why:** protected processes and anti-cheat forbid remote threads, by
  design.
- **Do:** out of scope for this library.

## External mode over exclusive fullscreen

- **You see:** the menu is invisible over a fullscreen game.
- **Why:** external mode is a topmost layered window; exclusive fullscreen
  bypasses the desktop compositor.
- **Do:** run the Target in borderless or windowed mode.

## Script HTTP capability is not implemented

- **You see:** nothing to call - no `http` binding exists.
- **Why:** the Sandbox capability vocabulary mentions HTTP, but the binding
  has not shipped.
- **Do:** `fs` is scoped to the configs folder; use it for local state.

## D3D12 Agility SDK titles (untested)

- **You see:** the DX12 overlay stays dormant with `dx12: waiting for the
  target queue`.
- **Why:** queue capture depends on the `D3D12CreateDevice` and factory
  exports; titles that route device creation through an app-local
  `D3D12Core` may bypass them.
- **Do:** use `injector --launch`; report the title with `overlay.log`.

## DPI and UI scale

- **You see:** a tiny or oversized menu on mixed-DPI setups.
- **Why:** scale follows the reported monitor DPI and is clamped to fit the
  window.
- **Do:** set `OVERLAY_UI_SCALE=1.5` (or any value) to override.

## Configs need a writable directory

- **You see:** `overlay_config_save` returns an error and the Configs page
  does not persist.
- **Why:** configs are written to `<dll dir>/configs/`.
- **Do:** keep the DLL in a writable directory (not under `Program Files`).
