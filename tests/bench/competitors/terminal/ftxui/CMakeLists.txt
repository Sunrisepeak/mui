# FTXUI's side of the terminal comparison (F4; ../README.md): the counter — a count and two buttons — as omui's
# tests/size/terminal shows it, built as a CMake project's release is (Release: -O3 -DNDEBUG), each function and
# object in its own section and the unreached ones not linked (as omui's programs are), then stripped.
# FTXUI is pinned: v7.0.3, fetched, or a checkout of that tag given as -DFTXUI_SOURCE=DIR.
cmake_minimum_required(VERSION 3.22)
project(ftxui_counter LANGUAGES CXX)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_compile_options(-ffunction-sections -fdata-sections)  # FTXUI's units too
if(FTXUI_SOURCE)
  add_subdirectory(${FTXUI_SOURCE} ftxui EXCLUDE_FROM_ALL)
else()
  include(FetchContent)
  FetchContent_Declare(ftxui GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI GIT_TAG v7.0.3 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(ftxui)
endif()
add_executable(ftxui-counter native/counter.cpp)
target_link_libraries(ftxui-counter PRIVATE ftxui::component)
target_link_options(ftxui-counter PRIVATE -Wl,--gc-sections)
