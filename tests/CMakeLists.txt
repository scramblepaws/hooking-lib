set(UI_TEST_SOURCES
  ${CMAKE_SOURCE_DIR}/src/core/log.cpp
  ${CMAKE_SOURCE_DIR}/src/core/paths.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/theme.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/widget.cpp)

add_library(overlay_abi_c_compile OBJECT abi_c_compile.c)
target_include_directories(overlay_abi_c_compile PRIVATE ${CMAKE_SOURCE_DIR}/include)
set_target_properties(overlay_abi_c_compile PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

add_library(overlay_abi_cpp_compile OBJECT abi_cpp_compile.cpp)
target_include_directories(overlay_abi_cpp_compile PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(overlay_abi_cpp_compile PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(unit_widget unit_widget.cpp ${UI_TEST_SOURCES})
target_include_directories(unit_widget PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(unit_widget PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_test(NAME unit_widget COMMAND unit_widget)

add_executable(unit_config unit_config.cpp
  ${CMAKE_SOURCE_DIR}/src/core/config.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/menu_model.cpp
  ${UI_TEST_SOURCES})
target_include_directories(unit_config PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(unit_config PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(unit_config PRIVATE yyjson)
add_test(NAME unit_config COMMAND unit_config)

add_executable(unit_style unit_style.cpp ${UI_TEST_SOURCES})
target_include_directories(unit_style PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(unit_style PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_test(NAME unit_style COMMAND unit_style)

add_executable(unit_script unit_script.cpp
  ${UI_TEST_SOURCES}
  ${CMAKE_SOURCE_DIR}/src/core/config.cpp
  ${CMAKE_SOURCE_DIR}/src/script/script.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/menu_model.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/notifications.cpp)
target_include_directories(unit_script PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(unit_script PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(unit_script PRIVATE yyjson imgui Luau.VM Luau.Compiler)
add_test(NAME unit_script COMMAND unit_script)
