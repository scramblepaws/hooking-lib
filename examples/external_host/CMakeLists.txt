add_executable(external_host WIN32 main.cpp)
target_include_directories(external_host PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(external_host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(external_host PRIVATE overlay)

if(MINGW)
  target_link_options(external_host PRIVATE -static -static-libgcc -static-libstdc++)
endif()
