cmake_minimum_required(VERSION 3.22)

# The first public binary release targets Apple Silicon on macOS 15. Keep the
# value explicit so changing Xcode or the build host cannot silently raise the
# minimum supported system.
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING
    "Minimum macOS version supported by packaged binaries")
endif()
project(ReactNativeSimulator VERSION 0.0.0 LANGUAGES C CXX)
set(RNS_RELEASE_CHANNEL "nightly")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR
   CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(
    "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
    "-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=.")
endif()
option(RNS_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(RNS_ENABLE_IMGUI
  "Build the optional Dear ImGui SDL3 diagnostics viewer" OFF)
option(RNS_ENABLE_SKIA
  "Use Skia + SkParagraph for the inspector's retained render surface" OFF)
option(RNS_ENABLE_SOURCE_DEVTOOLS_FRONTEND
  "Allow development builds to fall back to RN's source-tree DevTools frontend" OFF)

function(rns_enable_sanitizers target)
  if(RNS_ENABLE_SANITIZERS)
    target_compile_options(${target} PRIVATE
      -fsanitize=address,undefined -fno-sanitize=vptr
      -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
set(RNS_RN0732_FIXTURE_BUNDLE "" CACHE FILEPATH
  "Optional external RN 0.73.2 fixture bundle used by mutation integration tests")
set(RNS_RNTESTER_BUNDLE "" CACHE FILEPATH
  "Caller-built RN Tester Android bundle used by the default conformance integration")
option(RNS_REQUIRE_RNTESTER_BUNDLE
  "Fail configure when the caller-built RN Tester integration bundle is absent" OFF)

set(RNS_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(RNS_REACT_NATIVE_REPOSITORY "${RNS_THIRD_PARTY_DIR}/react-native")
set(RNS_REACT_NATIVE_DIR
    "${RNS_REACT_NATIVE_REPOSITORY}/packages/react-native")
set(RNS_HERMES_DIR "${RNS_THIRD_PARTY_DIR}/hermes")
set(RNS_FAST_FLOAT_DIR "${RNS_THIRD_PARTY_DIR}/fast_float")
set(RNS_IMGUI_DIR "${RNS_THIRD_PARTY_DIR}/imgui")
set(RNS_SDL_DIR "${RNS_THIRD_PARTY_DIR}/sdl")
set(RNS_SKIA_DIR "${RNS_THIRD_PARTY_DIR}/skia")
set(RNS_SKIA_OUT_DIR "${RNS_SKIA_DIR}/out/rnsim" CACHE PATH
  "GN output directory containing the pinned Skia static libraries")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DependencyVersions.cmake")

execute_process(
  COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse HEAD
  RESULT_VARIABLE RNS_GIT_COMMIT_RESULT
  OUTPUT_VARIABLE RNS_BUILD_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT RNS_GIT_COMMIT_RESULT EQUAL 0)
  set(RNS_BUILD_COMMIT "unknown")
endif()
execute_process(
  COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" status --porcelain
  RESULT_VARIABLE RNS_GIT_STATUS_RESULT
  OUTPUT_VARIABLE RNS_GIT_STATUS
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT RNS_GIT_STATUS_RESULT EQUAL 0)
  set(RNS_BUILD_DIRTY "unknown")
elseif(RNS_GIT_STATUS STREQUAL "")
  set(RNS_BUILD_DIRTY "false")
else()
  set(RNS_BUILD_DIRTY "true")
endif()

foreach(required_path
    "${RNS_REACT_NATIVE_DIR}/ReactCommon/react/runtime/ReactInstance.cpp"
    "${RNS_REACT_NATIVE_DIR}/sdks/.hermesv1version"
    "${RNS_HERMES_DIR}/API/hermes/hermes.h"
    "${RNS_FAST_FLOAT_DIR}/CMakeLists.txt")
  if(NOT EXISTS "${required_path}")
    message(FATAL_ERROR
      "Missing submodule source: ${required_path}\n"
      "Run: git submodule update --init --recursive")
  endif()
endforeach()

if(RNS_ENABLE_IMGUI)
  foreach(required_imgui_path
      "${RNS_IMGUI_DIR}/imgui.cpp"
      "${RNS_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
      "${RNS_IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp"
      "${RNS_SDL_DIR}/CMakeLists.txt"
      "${RNS_SDL_DIR}/include/SDL3/SDL_gpu.h")
    if(NOT EXISTS "${required_imgui_path}")
      message(FATAL_ERROR
        "Dear ImGui source is missing: ${required_imgui_path}\n"
        "Run: git submodule update --init --recursive")
    endif()
  endforeach()
  execute_process(
    COMMAND git -C "${RNS_IMGUI_DIR}" describe --tags --exact-match
    RESULT_VARIABLE RNS_IMGUI_TAG_RESULT
    OUTPUT_VARIABLE RNS_ACTUAL_IMGUI_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(NOT RNS_IMGUI_TAG_RESULT EQUAL 0 OR
     NOT RNS_ACTUAL_IMGUI_TAG STREQUAL RNS_EXPECTED_IMGUI_TAG)
    message(FATAL_ERROR
      "Dear ImGui submodule is ${RNS_ACTUAL_IMGUI_TAG}, but this project "
      "requires ${RNS_EXPECTED_IMGUI_TAG}.")
  endif()
  execute_process(
    COMMAND git -C "${RNS_SDL_DIR}" describe --tags --exact-match
    RESULT_VARIABLE RNS_SDL_TAG_RESULT
    OUTPUT_VARIABLE RNS_ACTUAL_SDL_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(NOT RNS_SDL_TAG_RESULT EQUAL 0 OR
     NOT RNS_ACTUAL_SDL_TAG STREQUAL RNS_EXPECTED_SDL_TAG)
    message(FATAL_ERROR
      "SDL submodule is ${RNS_ACTUAL_SDL_TAG}, but this project requires "
      "${RNS_EXPECTED_SDL_TAG}.")
  endif()
endif()

if(RNS_ENABLE_SKIA)
  if(NOT APPLE)
    message(FATAL_ERROR "The current Skia renderer bootstrap requires macOS")
  endif()
  foreach(required_skia_path
      "${RNS_SKIA_DIR}/include/core/SkCanvas.h"
      "${RNS_SKIA_DIR}/modules/skparagraph/include/Paragraph.h"
      "${RNS_SKIA_OUT_DIR}/libskia.a"
      "${RNS_SKIA_OUT_DIR}/libskparagraph.a")
    if(NOT EXISTS "${required_skia_path}")
      message(FATAL_ERROR
        "Skia source or GN output is missing: ${required_skia_path}\n"
        "See docs/guides/SKIA.md to bootstrap the optional renderer.")
    endif()
  endforeach()
  execute_process(
    COMMAND git -C "${RNS_SKIA_DIR}" rev-parse HEAD
    RESULT_VARIABLE RNS_SKIA_COMMIT_RESULT
    OUTPUT_VARIABLE RNS_ACTUAL_SKIA_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(NOT RNS_SKIA_COMMIT_RESULT EQUAL 0 OR
     NOT RNS_ACTUAL_SKIA_COMMIT STREQUAL RNS_EXPECTED_SKIA_COMMIT)
    message(FATAL_ERROR
      "Skia submodule is ${RNS_ACTUAL_SKIA_COMMIT}, but this project requires "
      "${RNS_EXPECTED_SKIA_COMMIT}.")
  endif()
endif()

string(REGEX REPLACE "^hermes-v" "" RNS_HERMES_VERSION
  "${RNS_EXPECTED_HERMES_TAG}")
file(READ "${RNS_REACT_NATIVE_DIR}/package.json" RNS_RN_PACKAGE_JSON)
string(JSON RNS_REACT_NATIVE_VERSION GET "${RNS_RN_PACKAGE_JSON}" version)
string(JSON RNS_REACT_PEER_RANGE GET
  "${RNS_RN_PACKAGE_JSON}" peerDependencies react)
execute_process(
  COMMAND git -C "${RNS_HERMES_DIR}" describe --tags --exact-match
  RESULT_VARIABLE RNS_HERMES_TAG_RESULT
  OUTPUT_VARIABLE RNS_ACTUAL_HERMES_TAG
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT RNS_HERMES_TAG_RESULT EQUAL 0 OR
   NOT RNS_ACTUAL_HERMES_TAG STREQUAL RNS_EXPECTED_HERMES_TAG)
  message(FATAL_ERROR
    "Hermes submodule is ${RNS_ACTUAL_HERMES_TAG}, but this React Native "
    "revision requires ${RNS_EXPECTED_HERMES_TAG}.")
endif()
execute_process(
  COMMAND git -C "${RNS_REACT_NATIVE_REPOSITORY}" rev-parse HEAD
  RESULT_VARIABLE RNS_RN_COMMIT_RESULT
  OUTPUT_VARIABLE RNS_ACTUAL_REACT_NATIVE_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT RNS_RN_COMMIT_RESULT EQUAL 0 OR
   NOT RNS_ACTUAL_REACT_NATIVE_COMMIT STREQUAL
       RNS_EXPECTED_REACT_NATIVE_COMMIT)
  message(FATAL_ERROR
    "React Native submodule is ${RNS_ACTUAL_REACT_NATIVE_COMMIT}, expected "
    "${RNS_EXPECTED_REACT_NATIVE_COMMIT}.")
endif()
execute_process(
  COMMAND git -C "${RNS_FAST_FLOAT_DIR}" describe --tags --exact-match
  RESULT_VARIABLE RNS_FAST_FLOAT_TAG_RESULT
  OUTPUT_VARIABLE RNS_ACTUAL_FAST_FLOAT_TAG
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT RNS_FAST_FLOAT_TAG_RESULT EQUAL 0 OR
   NOT RNS_ACTUAL_FAST_FLOAT_TAG STREQUAL RNS_EXPECTED_FAST_FLOAT_TAG)
  message(FATAL_ERROR
    "fast_float submodule is ${RNS_ACTUAL_FAST_FLOAT_TAG}, expected "
    "${RNS_EXPECTED_FAST_FLOAT_TAG}.")
endif()

set(JSI_DIR "${RNS_REACT_NATIVE_DIR}/ReactCommon/jsi" CACHE PATH "" FORCE)
set(HERMES_ENABLE_TEST_SUITE OFF CACHE BOOL "" FORCE)
set(HERMES_ENABLE_DEBUGGER ON CACHE BOOL "" FORCE)
set(HERMES_MEMORY_INSTRUMENTATION ON CACHE BOOL "" FORCE)
set(HERMES_BUILD_APPLE_FRAMEWORK OFF CACHE BOOL "" FORCE)
set(HERMES_BUILD_SHARED_JSI OFF CACHE BOOL "" FORCE)
if(RNS_ENABLE_SANITIZERS)
  # Instrument the pinned JS engine as well as the host. A host-only ASan lane
  # previously reported green only by suppressing a Hermes initialization
  # over-read.
  set(HERMES_ENABLE_ADDRESS_SANITIZER ON CACHE BOOL "" FORCE)
  set(HERMES_ENABLE_UNDEFINED_BEHAVIOR_SANITIZER ON CACHE BOOL "" FORCE)
endif()

add_subdirectory("${RNS_HERMES_DIR}" "${CMAKE_BINARY_DIR}/hermes")
add_subdirectory("${RNS_FAST_FLOAT_DIR}" "${CMAKE_BINARY_DIR}/fast_float")
if(RNS_ENABLE_IMGUI)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory("${RNS_SDL_DIR}" "${CMAKE_BINARY_DIR}/sdl")
endif()

set(RN_ROOT "${RNS_REACT_NATIVE_DIR}")
set(HERMES_SOURCE "${RNS_HERMES_DIR}")
set(HERMES_BINARY_DIR "${CMAKE_BINARY_DIR}/hermes")
add_subdirectory(runtime)

include(CTest)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
