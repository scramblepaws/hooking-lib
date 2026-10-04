add_executable(test_target WIN32 main.cpp)
target_compile_definitions(test_target PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(test_target PRIVATE d3d11 d3d12 dxgi)

if(MINGW)
  target_link_options(test_target PRIVATE -static -static-libgcc -static-libstdc++)
endif()
