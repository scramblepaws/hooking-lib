# Overlay Library

An internal library that renders a scriptable UI (Luau, Roblox-style) on top of a game or app process, across DX11/DX12/Vulkan, in injected and external modes.

Status: pre-v1. The native menu stack works end-to-end (injected DX11); scripting is next.

## What works today

- Injected DX11 overlay: MinHook trampolines on `Present`/`ResizeBuffers`, clean eject, WndProc reclaim
- DX12 support: the swapchain's device is detected and rendering is submitted on the Target's own present queue (captured by hooking the `CreateDXGIFactory*`/`D3D12CreateDevice` exports and swapping the per-instance `CreateCommandQueue` vtable entry). The DLL must be present before the Target creates its device - use `injector --launch` (or host mode); mid-run injection into an established DX12 device logs and stays dormant rather than crashing
- Hardened lifecycle: eject drains in-flight detours/messages before unload (no timing guesses); host-linked builds remove hooks but stay loaded; device-removed/reset triggers re-init; UI faults are contained (C++ exceptions; full SEH on MSVC builds)
- Borderless menu rendered through a backend-agnostic Canvas (no ImGui windows or widgets in the UI layer)
- Retained widgets: Toggle, Checkbox, Slider, Dropdown, Keybind, TextInput, ColorPicker
- Replaceable painters: any widget's rendering can be overridden per instance (C++ or C callback)
- Public C ABI (`include/overlay/overlay_c.h`): lifecycle, data-driven menu builder (pages/sections/widgets), widget values, painters + canvas, blank-mode chrome painter
- Starter menu (shipped default) and blank mode (consumer-drawn chrome), both over the same model
- Config persistence: named JSON snapshots keyed by widget `id`, saved to `<dll dir>/configs/`, autosave on eject, Configs page in the starter menu
- Notifications: top-corner toasts (position/duration configurable) plus a bottom event feed, drawn whether or not the menu is open
- Stylesheet layer: CSS subset (`menu {}`, `class {}`, `#id {}`) for colors and rounding; the shipped look is `assets/theme.css`
- Luau Script Host: sandboxed VM, `ui.*` menu/widget bindings, Lua painters over a Canvas, `task.wait/spawn/defer/delay`, `fs` scoped to the configs folder, mtime hot-reload. `scripts/starter.lua` builds the shipped menu; the C++ starter remains the fallback when the folder is empty
- Drawing API: script ESP primitives (`Drawing.new` for Line/Text/Circle/Square/Triangle) with `Vector2`/`Color3`, ZIndex ordering and transparency, always drawn above the game
- External mode: `overlay_init` with `OVERLAY_FLAG_EXTERNAL` gives the library its own topmost, click-through, color-keyed window over a target (or the primary monitor), running the same UI and script stack on its own thread. No injection, no hooks
- DPI fit-scaling (physical scale clamped so the layout never overlaps)
- Hotkeys: `Del` toggles the menu, `End` ejects
- Frame log at `build/bin/overlay.log`

## Not built yet

- Drawing `Image` kind (the other primitives are supported)
- Vulkan backend

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
- `test_target.exe` - minimal DX11 window to inject into (`--dx12` for the DX12 target)
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
```

The soak harness launches the target, injects, exercises open/click/drag/resize/close, ejects, and asserts the target survives and the eject drained cleanly. It moves the real mouse cursor, so run it when you are not using the machine.

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

## Layout

```
include/overlay/     public facade (init/shutdown/running) - thin so far
src/core/            context state, logging, module paths, config store
src/hook/            MinHook swapchain hooks, WndProc, input capture
src/render/          ImGui context + DX11 backend, font/DPI bootstrap
src/script/          Luau host: sandbox, scheduler, ui/fs bindings, hot reload
src/ui/              Canvas, widgets, painters, theme, notifications, menu
scripts/             *.lua loaded at startup and on change (the starter menu lives here)
examples/test_app/   DX11 target for smoke tests
tools/injector/      injector CLI
assets/fonts/        Roboto-Medium.ttf, fa-solid-900.ttf
assets/theme.css     shipped stylesheet
docs/adr/            architecture decisions
CONTEXT.md           glossary - read this before naming things
```

## Documentation

- [docs/custom-menus.md](docs/custom-menus.md) - how to build custom menus with the current widget stack
- [CONTEXT.md](CONTEXT.md) - canonical vocabulary (Widget, Painter, Canvas, Toast, ...)
- [docs/adr/](docs/adr/) - why ImGui is wrapped, why Luau, why behavior plus replaceable painters

## Licensing notes

Dear ImGui (MIT), MinHook (BSD-2), Luau (MIT when added), Roboto (Apache-2.0), Font Awesome Free (icons CC BY 4.0, fonts SIL OFL 1.1). If you redistribute builds externally, Font Awesome requires attribution.
