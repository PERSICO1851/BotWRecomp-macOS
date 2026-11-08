# Offline shader pipeline for the native renderer (docs/native-renderer.md).
#   cmake -G Ninja -S tools/shaders -B build-native/shaders-tool && ninja -C build-native/shaders-tool
#   build-native/shaders-tool/latte2msl --cemu runtime/cemu --out build-native/shaders <cache.bin>...
#   uv run python -I tools/shaders/compile_all.py build-native/shaders
cmake_minimum_required(VERSION 3.24)
project(latte_shaders CXX OBJCXX)

set(BOTW_HOMEBREW_PREFIX "$ENV{HOMEBREW_PREFIX}" CACHE PATH "Homebrew installation prefix")
if(NOT BOTW_HOMEBREW_PREFIX)
  find_program(BOTW_BREW_EXECUTABLE brew)
  if(BOTW_BREW_EXECUTABLE)
    execute_process(COMMAND "${BOTW_BREW_EXECUTABLE}" --prefix
      OUTPUT_VARIABLE BOTW_HOMEBREW_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()
endif()
if(BOTW_HOMEBREW_PREFIX)
  list(PREPEND CMAKE_PREFIX_PATH "${BOTW_HOMEBREW_PREFIX}")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_executable(latte2msl src/main.cpp src/inventory.cpp src/r600.cpp src/msl.cpp)
target_include_directories(latte2msl PRIVATE ${BOTW_HOMEBREW_PREFIX}/include)
target_compile_options(latte2msl PRIVATE -Wall -Wno-unused-function)
find_library(BOTW_ZSTD_LIBRARY NAMES zstd HINTS "${BOTW_HOMEBREW_PREFIX}/lib" REQUIRED)
target_link_libraries(latte2msl PRIVATE ${BOTW_ZSTD_LIBRARY} z)

# Metal-framework compile check (no offline toolchain needed)
add_executable(mtlcheck src/mtlcheck.mm)
target_compile_options(mtlcheck PRIVATE -fobjc-arc)
target_link_libraries(mtlcheck PRIVATE "-framework Metal" "-framework Foundation")
