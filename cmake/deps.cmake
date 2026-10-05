include(FetchContent)

# Pinned 2026-09-25 docking-branch head. Docking requires the branch, not a release tag.
set(OVERLAY_IMGUI_TAG 64944b4520b30772de8dbf0b37d0311746477a32)
set(OVERLAY_MINHOOK_TAG v1.3.3)

FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG ${OVERLAY_IMGUI_TAG})

# ponytail: SOURCE_SUBDIR points nowhere on purpose - MinHook's own CMakeLists
# targets policy versions CMake 4 rejects. We compile its handful of sources below.
FetchContent_Declare(minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG ${OVERLAY_MINHOOK_TAG}
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _not_a_cmake_project)

# Small C JSON parser/writer for the config store; we compile the single source ourselves.
FetchContent_Declare(yyjson
  GIT_REPOSITORY https://github.com/ibireme/yyjson.git
  GIT_TAG 0.10.0
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _not_a_cmake_project)

# Luau builds itself; only the VM and compiler targets get linked.
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(luau
  GIT_REPOSITORY https://github.com/luau-lang/luau.git
  GIT_TAG 0.741
  GIT_SHALLOW TRUE)

# Vulkan headers + volk loader. volk resolves the loader at runtime, so no
# import library is needed; volk.c is compiled directly like MinHook's sources.
FetchContent_Declare(vulkan_headers
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
  GIT_TAG vulkan-sdk-1.4.304.1
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _not_a_cmake_project)
FetchContent_Declare(volk
  GIT_REPOSITORY https://github.com/zeux/volk.git
  GIT_TAG vulkan-sdk-1.4.304.1
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _not_a_cmake_project)

FetchContent_MakeAvailable(imgui minhook yyjson luau vulkan_headers volk)

add_library(volk STATIC ${volk_SOURCE_DIR}/volk.c)
target_include_directories(volk PUBLIC ${volk_SOURCE_DIR} ${vulkan_headers_SOURCE_DIR}/include)
target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(imgui PUBLIC d3d11 d3d12 dxgi d3dcompiler dwmapi volk)

add_library(minhook STATIC
  ${minhook_SOURCE_DIR}/src/buffer.c
  ${minhook_SOURCE_DIR}/src/hook.c
  ${minhook_SOURCE_DIR}/src/trampoline.c
  ${minhook_SOURCE_DIR}/src/hde/hde64.c)
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)
target_compile_definitions(minhook PUBLIC MH_STATIC)

add_library(yyjson STATIC ${yyjson_SOURCE_DIR}/src/yyjson.c)
target_include_directories(yyjson PUBLIC ${yyjson_SOURCE_DIR}/src)
