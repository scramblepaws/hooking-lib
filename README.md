# Overlay Library

An internal library that renders a scriptable UI (Luau, Roblox-style) on top of a game or app process, across DX11/DX12/Vulkan, in injected and external modes.

Status: pre-v1. The full stack (native UI, scripts, configs, notifications, Drawing) works end-to-end on DX11/DX12/Vulkan injected targets and in external mode.

## What works today

- Injected DX11 overlay: MinHook trampolines on `Present`/`ResizeBuffers`, clean eject, WndProc reclaim
- DX12 support: the swapchain's device is detected and rendering is submitted on the Target's own present queue (captured by hooking the `CreateDXGIFactory*`/`D3D12CreateDevice` exports and swapping the per-instance `CreateCommandQueue` vtable entry). The DLL must be present before the Target creates its device - use `injector --launch` (or host mode); mid-run injection into an established DX12 device logs and stays dormant rather than crashing
- Vulkan support: the loader's `vkCreateInstance`/`vkCreateDevice`/`vkCreateWin32SurfaceKHR`/`vkCreateSwapchainKHR`/`vkQueuePresentKHR` entry points are hooked, and `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr` hand out our detours so dispatch tables built at runtime (volk et al) stay on our path. The overlay draws into the presented swapchain image at `vkQueuePresentKHR` time on the Target's own queue. A Target destroying and recreating its device (TDR, GPU switch, engine re-init) is detected at `vkDestroyDevice` and the renderer rebinds to the replacement automatically. Once the Target has cached our pointers, eject turns the hooks off and stops rendering but leaves the module loaded. The DLL must be present before the Target resolves its Vulkan pointers - use `injector --launch`; mid-run injection into an established Vulkan app logs and stays dormant
- Hardened lifecycle: eject drains in-flight detours/messages before unload (no timing guesses); host-linked builds remove hooks but stay loaded; device-removed/reset triggers re-init; render and painter faults (access violations included) are contained on any toolchain by a first-chance vectored handler plus a setjmp firewall - rendering is abandoned and the Target keeps running
- Borderless menu rendered through a backend-agnostic Canvas (no ImGui windows or widgets in the UI layer)
- Fixed menu design size (1080x640 base, DPI-scaled) so the Overlay looks identical in every Target; shrunk only when the Target window cannot fit it
- Per-widget sub-settings behind the gear: accent colour override, a key bind that toggles/activates the widget, and reset
- Retained widgets: Toggle, Checkbox, Slider, Dropdown, Keybind, TextInput, ColorPicker
- Replaceable painters: any widget's rendering can be overridden per instance (C++ or C callback)
- Public C ABI (`include/overlay/overlay_c.h`): lifecycle, data-driven menu builder (pages/sections/widgets), widget values, painters + canvas, blank-mode chrome painter
- No built-in menu content: an Overlay starts empty (the chrome frame plus a placeholder), and the pages/widgets come from the host through the C ABI or from scripts you drop in `scripts/`. Blank mode replaces the chrome too (consumer-drawn)
- Config persistence: named JSON snapshots keyed by widget `id`, saved to `<dll dir>/configs/`, autosave on eject; the Configs page pattern (a page titled `Configs` with `config_name`/`config_save`/`config_load` widgets) is handled by the library
- Notifications: top-corner toasts (position/duration configurable) plus a bottom event feed, drawn whether or not the menu is open
- Stylesheet layer: CSS subset (`menu {}`, `class {}`, `#id {}`) for colors and rounding; the shipped look is `assets/theme.css`
- Luau Script Host: sandboxed VM, `ui.*` menu/widget bindings, Lua painters over a Canvas, `task.wait/spawn/defer/delay`, `fs` scoped to the configs folder, mtime hot-reload. Scripts own the menu when present; with no scripts the Overlay stays empty
- Drawing API: script ESP primitives via `Drawing.new` (Line/Text/Circle/Square/Triangle/Image) with `Vector2`/`Color3`, ZIndex ordering and transparency, always drawn above the game. Images decode with Windows WIC from files relative to the scripts folder and upload to whichever backend is active (DX11/DX12/Vulkan), bounded per session by the backend's overlay texture slots
- C# binding (`bindings/csharp/OverlayLib`): P/Invoke over the C ABI with handle wrappers, delegates for painters and a runnable smoke example
- External mode: `overlay_init` with `OVERLAY_FLAG_EXTERNAL` gives the library its own topmost, click-through, color-keyed window over a target (or the primary monitor), running the same UI and script stack on its own thread. No injection, no hooks
- DPI fit-scaling (physical scale clamped so the layout never overlaps)
- Hotkeys: `Del` toggles the menu, `End` ejects
- Mouse handling while the menu is open: the Target's `ClipCursor` is lifted and `SetCursorPos` recentring (raw mouse look) is blocked, so the pointer can reach the UI; the software cursor is drawn by ImGui and the Target's clip is restored when the menu closes
- Frame log at `build/bin/overlay.log`

## Requirements

- Windows x64
- CMake 3.24+, Ninja
- MinGW-w64 GCC (tested with 16.1 UCRT) or MSVC (untested)
- Network on first configure (CMake FetchContent pulls Dear ImGui docking branch and MinHook)

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build
```

Outputs (in `build/bin/`):

- `overlay.dll` - the injected overlay
- `test_target.exe` - minimal window to inject into (`--dx12` or `--vulkan`, and `--vk-recreate` to cycle the Vulkan device mid-run)
- `sdk_consumer.exe` - public-API example: links the DLL, builds a custom menu, blank-mode chrome
- `external_host.exe` - external mode: topmost click-through overlay over the primary monitor; scripts build the menu; Del toggles, End exits
- `injector.exe` - remote LoadLibrary injector
- `assets/fonts/` - Roboto + Font Awesome, copied automatically per build

## Run it

```sh
.\build\bin\test_target.exe
.\build\bin\injector.exe test_target     # pid or exe name; dll defaults to overlay.dll
# in the target window: Del = menu, End = eject
```

Injector details:

- `injector [--no-wait] <pid or exe name> [path-to-overlay.dll]`
- `injector [--no-wait] --launch <exe> [args...]` - starts the target **suspended**, injects, waits for the overlay to signal its hooks are installed, then resumes. This is the reliable path for DX12 (and anything created at startup), no timing games required.
- Launched with no args (double-click) it prompts for the target and defaults the DLL next to itself.
- Relative DLL paths are resolved to absolute; `LoadLibraryA` runs in the target, where relative paths would break.

## Environment

- `OVERLAY_UI_SCALE=1.5` - forces the UI scale, overriding monitor DPI. Test hook and a workaround for games that lie about DPI.

## Tests

```sh
ctest --test-dir build --output-on-failure          # unit tests
powershell -ExecutionPolicy Bypass -File tools/soak/soak.ps1 -Cycles 5
powershell -ExecutionPolicy Bypass -File tools/ship-check.ps1   # release gate
```

`tools/ship-check.ps1` is the release gate: it builds, runs the unit tests, proves fault containment, injects into DX11/DX12/Vulkan targets (including a mid-run Vulkan device replacement), runs the soak, and runs the C# smoke test, then prints a PASS/FAIL summary. `-SkipGpu` skips everything that needs a desktop GPU session, `-SkipBuild` assumes the last build.

The soak harness launches the target, injects, exercises open/click/drag/resize/close, ejects, and asserts the target survives and the eject drained cleanly. It moves the real mouse cursor, so run it when you are not using the machine.

Fault containment has its own checks: `unit_salvage` proves null writes and divide-by-zero unwind out of the firewall, and `sdk_consumer.exe --crash-painter` crashes the live chrome painter inside the hooked Present path and asserts the process survives with rendering disabled.

## SDK packaging

```sh
powershell -ExecutionPolicy Bypass -File tools/package.ps1
# -> build/dist/overlay-sdk-<version>-win64.zip
# -> build/dist/overlay-runner-<version>-win64.zip
```

The SDK installs headers, `overlay.dll` (+ assets), the import library and a CMake package. Consumers (the stable surface is the C ABI in `overlay_c.h`):

```cmake
find_package(overlay CONFIG REQUIRED)
target_link_libraries(app PRIVATE overlay::overlay d3d11 dxgi)
```

```sh
cmake -S app -B app/build -G Ninja -DCMAKE_PREFIX_PATH=C:/path/to/overlay-sdk-0.1.0-win64
```

The version lives in `include/overlay/overlay_c.h` and flows into the CMake package, so artifacts and `overlay_version_string()` cannot drift. CI (`.github/workflows/ci.yml`) builds and unit-tests on GitHub with MinGW and MSVC; the smoke/soak harness needs a desktop GPU session and stays a local/manual check.

## C# host

`bindings/csharp/OverlayLib` wraps the C ABI for .NET 8+; `bindings/csharp/Example` is a runnable smoke test (creates widgets, round-trips a config, parses a stylesheet, checks the ABI version).

```sh
dotnet run --project bindings/csharp/Example -c Release
```

The example copies `build/bin/overlay.dll` and `assets/` next to its binary, so run a CMake build first. A host looks like:

```csharp
using OverlayLib;

Overlay.Init(Backend.Auto, Overlay.FlagExternal, menuTitle: "My host");
var page = Overlay.Context.CreatePage("Main", "Host");
var section = page.CreateSection("Settings");
var esp = section.CreateWidget(WidgetKind.Toggle, "esp", "ESP");
esp.SetBool(true);
Overlay.Notify("host attached");
```

## Layout

```
include/overlay/     public facade (init/shutdown/running) - thin so far
src/core/            context state, logging, module paths, config store
src/hook/            MinHook swapchain hooks, WndProc, input capture
src/render/          ImGui context + DX11/DX12/Vulkan backends, textures, font/DPI bootstrap
src/script/          Luau host: sandbox, scheduler, ui/fs bindings, hot reload
src/ui/              Canvas, widgets, painters, theme, notifications, menu
scripts/             *.lua loaded at startup and hot-reloaded; the library ships no scripts of its own
examples/test_app/   DX11/DX12/Vulkan target for smoke tests
bindings/csharp/     .NET wrapper over the C ABI + smoke example
tools/injector/      injector CLI
assets/fonts/        Roboto-Medium.ttf, fa-solid-900.ttf
assets/theme.css     shipped stylesheet
docs/adr/            architecture decisions
CONTEXT.md           glossary - read this before naming things
```

## Documentation

- [docs/custom-menus.md](docs/custom-menus.md) - how to build custom menus with the current widget stack
- [docs/known-issues.md](docs/known-issues.md) - every known limit, what you will see, and what to do
- [CONTEXT.md](CONTEXT.md) - canonical vocabulary (Widget, Painter, Canvas, Toast, ...)
- [docs/adr/](docs/adr/) - why ImGui is wrapped, why Luau, why behavior plus replaceable painters

## Licensing notes

Dear ImGui (MIT), MinHook (BSD-2), Luau (MIT when added), Roboto (Apache-2.0), Font Awesome Free (icons CC BY 4.0, fonts SIL OFL 1.1). If you redistribute builds externally, Font Awesome requires attribution.
