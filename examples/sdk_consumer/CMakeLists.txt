add_executable(sdk_consumer WIN32 main.cpp)
target_include_directories(sdk_consumer PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(sdk_consumer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(sdk_consumer PRIVATE overlay d3d11 dxgi)

if(MINGW)
  target_link_options(sdk_consumer PRIVATE -static -static-libgcc -static-libstdc++)
endif()
