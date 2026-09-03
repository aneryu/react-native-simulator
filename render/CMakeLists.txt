# Skia retained renderer.
#
# Defines the prepared-paragraph text engine shared by Yoga measurement and
# paint, plus the mounted-tree painter and PNG exporter consumed by direct
# screenshots, interactive frames, and diagnostics. Added by
# runtime/CMakeLists.txt when RNS_ENABLE_SKIA is set; wiring into the engine
# and the rnsim executable stays with those targets.

set(RNS_SKIA_ARCHIVES
  "${RNS_SKIA_OUT_DIR}/libskparagraph.a"
  "${RNS_SKIA_OUT_DIR}/libskshaper.a")
# Release builds require the compact archive from the host Skia bootstrap.
# Keep the full upstream archive as an explicit development fallback so an old
# local Skia tree can still configure, but never let release packaging silently
# regain the ~9 MiB data payload.
if(EXISTS "${RNS_SKIA_OUT_DIR}/libskunicode_icu_rnsim.a")
  list(APPEND RNS_SKIA_ARCHIVES "${RNS_SKIA_OUT_DIR}/libskunicode_icu_rnsim.a")
elseif(EXISTS "${RNS_SKIA_OUT_DIR}/libskunicode_icu.a")
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR
      "Release requires filtered Skia ICU data. Run cmake/bootstrap-skia-macos.sh or cmake/bootstrap-skia-linux.sh")
  endif()
  message(WARNING
    "Using Skia's full ICU data; run the host Skia bootstrap for compact output")
  list(APPEND RNS_SKIA_ARCHIVES "${RNS_SKIA_OUT_DIR}/libskunicode_icu.a")
else()
  message(FATAL_ERROR
    "Missing Skia unicode ICU archive in ${RNS_SKIA_OUT_DIR}")
endif()
list(APPEND RNS_SKIA_ARCHIVES
  "${RNS_SKIA_OUT_DIR}/libskunicode_core.a"
  "${RNS_SKIA_OUT_DIR}/libharfbuzz.a"
  "${RNS_SKIA_OUT_DIR}/libicu.a"
  "${RNS_SKIA_OUT_DIR}/libskia.a"
  "${RNS_SKIA_OUT_DIR}/libfreetype2.a"
  "${RNS_SKIA_OUT_DIR}/libskcms.a"
  "${RNS_SKIA_OUT_DIR}/libzlib.a"
  "${RNS_SKIA_OUT_DIR}/libpng.a")
# partition_alloc archives exist only when Skia is built with Clang.
foreach(optional_skia_archive
    "${RNS_SKIA_OUT_DIR}/libraw_ptr.a"
    "${RNS_SKIA_OUT_DIR}/liballocator_shim.a"
    "${RNS_SKIA_OUT_DIR}/liballocator_core.a"
    "${RNS_SKIA_OUT_DIR}/liballocator_base.a")
  if(EXISTS "${optional_skia_archive}")
    list(APPEND RNS_SKIA_ARCHIVES "${optional_skia_archive}")
  endif()
endforeach()
if(NOT APPLE)
  # FontConfig/custom-directory managers compile into libskia.a on this pin.
  # JPEG/WebP stay separate archives because Linux enables those decoders.
  foreach(linux_skia_archive
      "${RNS_SKIA_OUT_DIR}/libjpeg.a"
      "${RNS_SKIA_OUT_DIR}/libjpeg12.a"
      "${RNS_SKIA_OUT_DIR}/libjpeg16.a"
      "${RNS_SKIA_OUT_DIR}/libwebp.a"
      "${RNS_SKIA_OUT_DIR}/libwebp_sse41.a"
      "${RNS_SKIA_OUT_DIR}/libwebp_sse42.a"
      "${RNS_SKIA_OUT_DIR}/libexpat.a"
      "${RNS_SKIA_OUT_DIR}/libwuffs.a")
    if(EXISTS "${linux_skia_archive}")
      list(APPEND RNS_SKIA_ARCHIVES "${linux_skia_archive}")
    endif()
  endforeach()
endif()
foreach(skia_archive IN LISTS RNS_SKIA_ARCHIVES)
  if(NOT EXISTS "${skia_archive}")
    message(FATAL_ERROR "Missing Skia archive: ${skia_archive}")
  endif()
endforeach()

add_library(react-native-simulator-skia-text STATIC
  "${CMAKE_CURRENT_LIST_DIR}/SkiaTextLayoutEngine.cpp")
target_include_directories(react-native-simulator-skia-text
  PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
    "${RNS_SKIA_DIR}"
    "${RNS_SKIA_DIR}/third_party/externals/icu/source/common")
target_link_libraries(react-native-simulator-skia-text
  PUBLIC
    ${RNS_SKIA_ARCHIVES})
if(APPLE)
  target_link_libraries(react-native-simulator-skia-text PUBLIC
    "-framework CoreText"
    "-framework CoreGraphics"
    "-framework Foundation")
else()
  find_package(Fontconfig REQUIRED)
  target_link_libraries(react-native-simulator-skia-text PUBLIC
    Fontconfig::Fontconfig
    ${CMAKE_DL_LIBS}
    m)
endif()
target_compile_options(react-native-simulator-skia-text PRIVATE
  -Wall -Wextra -Wpedantic)
target_compile_definitions(react-native-simulator-skia-text PRIVATE
  U_DISABLE_RENAMING=1
  U_STATIC_IMPLEMENTATION=1)
set_target_properties(react-native-simulator-skia-text PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
rns_enable_sanitizers(react-native-simulator-skia-text)
if(RNS_ENABLE_SANITIZERS AND NOT APPLE)
  # Prebuilt Skia archives are not vptr-instrumented. Linux GCC UBSan vptr
  # emits typeinfo references those archives cannot satisfy. Keep vptr on
  # engine/Yoga TUs so LayoutConformance typeinfo is not discarded.
  target_compile_options(react-native-simulator-skia-text PRIVATE
    -fno-sanitize=vptr)
endif()

add_library(react-native-simulator-skia-renderer STATIC
  "${CMAKE_CURRENT_LIST_DIR}/SkiaMountedTreeRenderer.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/SkiaPngExporter.cpp")
target_include_directories(react-native-simulator-skia-renderer
  PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
    "${PROJECT_ROOT}/runtime/include"
  PRIVATE
    "${RNS_SKIA_DIR}"
    "${FOLLY_INCLUDE_DIR}")
target_link_libraries(react-native-simulator-skia-renderer
  PUBLIC react-native-simulator-skia-text
  PRIVATE rns_folly)
if(APPLE)
  target_link_libraries(react-native-simulator-skia-renderer PRIVATE
    "-framework Foundation"
    "-framework ImageIO"
    "-framework CoreGraphics")
endif()
target_compile_options(react-native-simulator-skia-renderer PRIVATE
  -Wall -Wextra -Wpedantic)
rns_enable_sanitizers(react-native-simulator-skia-renderer)
if(RNS_ENABLE_SANITIZERS AND NOT APPLE)
  target_compile_options(react-native-simulator-skia-renderer PRIVATE
    -fno-sanitize=vptr)
endif()
