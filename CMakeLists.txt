cmake_minimum_required(VERSION 3.24)

# Version single source of truth: the public header.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/include/overlay/overlay_c.h OVERLAY_VERSION_HEADER)
string(REGEX MATCH "#define OVERLAY_VERSION_MAJOR ([0-9]+)" _ "${OVERLAY_VERSION_HEADER}")
set(OVERLAY_VERSION_MAJOR ${CMAKE_MATCH_1})
string(REGEX MATCH "#define OVERLAY_VERSION_MINOR ([0-9]+)" _ "${OVERLAY_VERSION_HEADER}")
set(OVERLAY_VERSION_MINOR ${CMAKE_MATCH_1})
string(REGEX MATCH "#define OVERLAY_VERSION_PATCH ([0-9]+)" _ "${OVERLAY_VERSION_HEADER}")
set(OVERLAY_VERSION_PATCH ${CMAKE_MATCH_1})

project(overlay VERSION ${OVERLAY_VERSION_MAJOR}.${OVERLAY_VERSION_MINOR}.${OVERLAY_VERSION_PATCH} LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "overlay targets 64-bit Windows only")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)

enable_testing()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps.cmake)

add_subdirectory(src)
add_subdirectory(examples/test_app)
add_subdirectory(examples/sdk_consumer)
add_subdirectory(examples/external_host)
add_subdirectory(tools/injector)
add_subdirectory(tests)

include(CMakePackageConfigHelpers)

install(TARGETS overlay
  EXPORT overlayTargets
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib)

install(TARGETS injector RUNTIME DESTINATION bin)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION bin)
install(FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/include/overlay/overlay_c.h
  DESTINATION include/overlay)
install(EXPORT overlayTargets NAMESPACE overlay:: DESTINATION lib/cmake/overlay)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/overlayConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/overlayConfig.cmake
  INSTALL_DESTINATION lib/cmake/overlay)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/overlayConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/overlayConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/overlayConfigVersion.cmake
  DESTINATION lib/cmake/overlay)
