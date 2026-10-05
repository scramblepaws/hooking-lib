cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(overlay_sdk_consumer LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

add_executable(sdk_consumer WIN32 main.cpp)
target_compile_definitions(sdk_consumer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

if(OVERLAY_SDK_DIR)
  find_package(overlay CONFIG REQUIRED PATHS "${OVERLAY_SDK_DIR}" NO_DEFAULT_PATH)
  target_link_libraries(sdk_consumer PRIVATE overlay::overlay d3d11 dxgi)
else()
  target_include_directories(sdk_consumer PRIVATE ${CMAKE_SOURCE_DIR}/include)
  target_link_libraries(sdk_consumer PRIVATE overlay d3d11 dxgi)
endif()

if(MINGW)
  target_link_options(sdk_consumer PRIVATE -static -static-libgcc -static-libstdc++)
endif()
