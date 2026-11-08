cmake_minimum_required(VERSION 3.24)

# Set Apple toolchain properties before project() initializes its compiler checks.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "$ENV{BOTW_MACOS_DEPLOYMENT_TARGET}")
endif()
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  find_program(BOTW_SW_VERS_EXECUTABLE sw_vers)
  if(BOTW_SW_VERS_EXECUTABLE)
    execute_process(COMMAND "${BOTW_SW_VERS_EXECUTABLE}" -productVersion
      OUTPUT_VARIABLE CMAKE_OSX_DEPLOYMENT_TARGET OUTPUT_STRIP_TRAILING_WHITESPACE)
  else()
    set(CMAKE_OSX_DEPLOYMENT_TARGET 13.3)
  endif()
endif()
set(CMAKE_OSX_ARCHITECTURES arm64)

project(BotWRecomp LANGUAGES C CXX OBJC OBJCXX)

# Homebrew uses different prefixes on Apple Silicon and Intel Macs. Resolve the active
# installation rather than baking one developer's package-manager path into this build.
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
find_package(OpenSSL REQUIRED COMPONENTS Crypto HINTS "${BOTW_HOMEBREW_PREFIX}/opt/openssl@3")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Recompiled guest code (tools/recompile.py writes generated/).
file(GLOB GUEST_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/generated/fn_*.cpp
  ${CMAKE_SOURCE_DIR}/generated/big_*.cpp
  ${CMAKE_SOURCE_DIR}/generated/huge_*.cpp
  ${CMAKE_SOURCE_DIR}/generated/table.cpp
  ${CMAKE_SOURCE_DIR}/generated/hle_stubs.cpp)
list(FILTER GUEST_SOURCES EXCLUDE REGEX "/\\._")
add_library(guest OBJECT ${GUEST_SOURCES})
target_include_directories(guest PRIVATE runtime/include generated)
target_compile_options(guest PRIVATE -O2 -gline-tables-only -w -fwrapv -fno-strict-aliasing -mcpu=apple-m1
  -ffp-contract=off)
file(GLOB BIG_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/generated/big_*.cpp)
file(GLOB HUGE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/generated/huge_*.cpp)
set_source_files_properties(${BIG_SOURCES} PROPERTIES COMPILE_OPTIONS "-O1")
set_source_files_properties(${HUGE_SOURCES} PROPERTIES COMPILE_OPTIONS "-O0")

# Vendored Cemu Latte GPU layer (runtime/cemu, MPL-2.0) + shims, built in Cemu's own environment.
file(GLOB_RECURSE CEMU_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/runtime/cemu/Cafe/*.cpp ${CMAKE_SOURCE_DIR}/runtime/cemu/Cafe/*.mm
  ${CMAKE_SOURCE_DIR}/runtime/cemu/Common/*.cpp ${CMAKE_SOURCE_DIR}/runtime/cemu/util/*.cpp
  ${CMAKE_SOURCE_DIR}/runtime/cemu/Cemu/FileCache/*.cpp
  ${CMAKE_SOURCE_DIR}/runtime/cemu/imgui/*.mm
  ${CMAKE_SOURCE_DIR}/runtime/cemu_shims/*.cpp ${CMAKE_SOURCE_DIR}/runtime/cemu_shims/*.mm)
list(FILTER CEMU_SOURCES EXCLUDE REGEX "LatteOverlay\\.cpp$|/dependencies/|SystemInfo(Win|Linux|BSD)\\.cpp$|/\\._")
list(APPEND CEMU_SOURCES
  ${CMAKE_SOURCE_DIR}/runtime/cemu/dependencies/imgui/imgui.cpp
  ${CMAKE_SOURCE_DIR}/runtime/cemu/dependencies/imgui/imgui_draw.cpp
  ${CMAKE_SOURCE_DIR}/runtime/cemu/dependencies/imgui/imgui_tables.cpp
  ${CMAKE_SOURCE_DIR}/runtime/cemu/dependencies/imgui/imgui_widgets.cpp)
add_library(cemu_gpu OBJECT ${CEMU_SOURCES})
target_include_directories(cemu_gpu PRIVATE runtime/cemu_shims runtime/cemu runtime/cemu/Cafe
  runtime/cemu/dependencies/metal-cpp runtime/cemu/dependencies/imgui ${BOTW_HOMEBREW_PREFIX}/include
  ${OPENSSL_INCLUDE_DIR})
target_compile_definitions(cemu_gpu PRIVATE BOOST_NOWIDE_NO_LIB ENABLE_METAL BOTWRECOMP IMGUI_IMPL_METAL_CPP
  EMULATOR_VERSION_MAJOR=2 EMULATOR_VERSION_MINOR=6 EMULATOR_VERSION_PATCH=0 EMULATOR_HASH=a53cf0e)
target_compile_options(cemu_gpu PRIVATE -w -O2 -g1 "SHELL:-include Common/precompiled.h")

file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/runtime/src/*.cpp
  ${CMAKE_SOURCE_DIR}/runtime/src/*.mm)
list(FILTER RUNTIME_SOURCES EXCLUDE REGEX "/\\._")
add_executable(BotWRecomp ${RUNTIME_SOURCES} $<TARGET_OBJECTS:guest> $<TARGET_OBJECTS:cemu_gpu>)
target_include_directories(BotWRecomp PRIVATE runtime/include runtime/src generated
  ${BOTW_HOMEBREW_PREFIX}/include ${OPENSSL_INCLUDE_DIR})
target_compile_options(BotWRecomp PRIVATE -Wall -Wno-unused-function -Wno-unused-variable -fwrapv -mcpu=apple-m1
  $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
find_package(SDL3 REQUIRED CONFIG)
find_package(fmt REQUIRED CONFIG)
find_library(BOTW_ZSTD_LIBRARY NAMES zstd HINTS "${BOTW_HOMEBREW_PREFIX}/lib" REQUIRED)
target_link_libraries(BotWRecomp PRIVATE z SDL3::SDL3 fmt::fmt OpenSSL::Crypto ${BOTW_ZSTD_LIBRARY}
  "-framework Metal" "-framework QuartzCore" "-framework Foundation" "-framework AppKit" "-framework MetalFX"
  "-framework CoreGraphics" "-framework ImageIO" "-framework UniformTypeIdentifiers" "-framework VideoToolbox" "-framework CoreMedia" "-framework CoreVideo")

# Native Metal GPU path (docs/native-renderer.md): option BOTW_NATIVE_GPU, default OFF (gpu_native.cmake).
include(${CMAKE_SOURCE_DIR}/runtime/src/gpu_native/gpu_native.cmake OPTIONAL)
