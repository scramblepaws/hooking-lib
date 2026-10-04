add_executable(injector main.cpp)
target_compile_definitions(injector PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MINGW)
  target_link_options(injector PRIVATE -static -static-libgcc -static-libstdc++)
endif()
