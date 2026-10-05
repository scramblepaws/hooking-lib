add_library(overlay SHARED
  c_api/overlay_c.cpp
  core/config.cpp
  core/log.cpp
  core/overlay.cpp
  core/paths.cpp
  external/external.cpp
  hook/hook.cpp
  render/backend.cpp
  render/dx11.cpp
  render/dx12.cpp
  render/textures.cpp
  render/vulkan.cpp
  script/script.cpp
  ui/canvas.cpp
  ui/menu.cpp
  ui/menu_model.cpp
  ui/notifications.cpp
  ui/starter_menu.cpp
  ui/theme.cpp
  ui/widget.cpp)

target_include_directories(overlay PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_include_directories(overlay PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(overlay PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE OVERLAY_BUILD)
target_link_libraries(overlay PRIVATE imgui minhook yyjson Luau.VM Luau.Compiler d3d12 windowscodecs volk)

set_target_properties(overlay PROPERTIES
  OUTPUT_NAME overlay
  PREFIX ""
  VERSION ${PROJECT_VERSION})

if(MINGW)
  target_compile_options(overlay PRIVATE -Wall -Wextra)
  # static runtimes: the DLL gets injected into processes that have no MinGW runtime
  target_link_options(overlay PRIVATE -static -static-libgcc -static-libstdc++)
endif()

add_custom_command(TARGET overlay POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets
          $<TARGET_FILE_DIR:overlay>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/scripts
          $<TARGET_FILE_DIR:overlay>/scripts)
