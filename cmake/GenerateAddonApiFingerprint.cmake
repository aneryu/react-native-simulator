# Compatibility fingerprint for ABI 4 MODULE loading. Not a trust signal.

function(rns_generate_addon_api_fingerprint)
  set(_headers)
  file(GLOB _headers
    "${PROJECT_SOURCE_DIR}/runtime/include/react-native-simulator/*.h")
  list(SORT _headers)
  set(_document "")
  foreach(_header IN LISTS _headers)
    file(SHA256 "${_header}" _hash)
    get_filename_component(_name "${_header}" NAME)
    string(APPEND _document "${_name} ${_hash}\n")
  endforeach()
  string(APPEND _document
    "reactNative ${RNS_REACT_NATIVE_VERSION}\n"
    "hermes ${RNS_HERMES_VERSION}\n"
    "reactNativeCommit ${RNS_ACTUAL_REACT_NATIVE_COMMIT}\n"
    "hermesCommit ${RNS_ACTUAL_HERMES_COMMIT}\n")
  string(APPEND _document
    "compiler ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n"
    "cxxStandard ${CMAKE_CXX_STANDARD}\n")
  if(APPLE)
    set(_stdlib "libc++")
  elseif(CMAKE_CXX_FLAGS MATCHES "-stdlib=libc\\+\\+")
    set(_stdlib "libc++")
  else()
    set(_stdlib "libstdc++")
  endif()
  string(APPEND _document "stdlib ${_stdlib}\n")
  if(CMAKE_BUILD_TYPE)
    set(_build_type "${CMAKE_BUILD_TYPE}")
  else()
    set(_build_type "Release")
  endif()
  string(APPEND _document "buildType ${_build_type}\n")
  if(_build_type STREQUAL "Debug")
    string(APPEND _document "ndebug 0\n")
  else()
    string(APPEND _document "ndebug 1\n")
  endif()
  if(APPLE)
    set(_cxx11_abi "n/a")
  elseif(CMAKE_CXX_FLAGS MATCHES "_GLIBCXX_USE_CXX11_ABI=0")
    set(_cxx11_abi "0")
  else()
    set(_cxx11_abi "1")
  endif()
  string(APPEND _document "cxx11Abi ${_cxx11_abi}\n")
  if(RNS_ENABLE_SANITIZERS)
    set(_sanitizer "address,undefined,no-vptr")
  else()
    set(_sanitizer "none")
  endif()
  string(APPEND _document
    "sanitizer ${_sanitizer}\n"
    "visibility hidden\n")
  set(_doc_file "${CMAKE_CURRENT_BINARY_DIR}/generated/AddonApiFingerprint.txt")
  file(WRITE "${_doc_file}" "${_document}")
  file(SHA256 "${_doc_file}" RNS_ADDON_API_FINGERPRINT)
  string(TOLOWER "${RNS_ADDON_API_FINGERPRINT}" RNS_ADDON_API_FINGERPRINT)
  set(RNS_ADDON_API_FINGERPRINT "${RNS_ADDON_API_FINGERPRINT}" PARENT_SCOPE)
  file(WRITE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/AddonApiFingerprint.h"
    "#pragma once\ninline constexpr const char kSimulatorAddonApiFingerprint[] = \"${RNS_ADDON_API_FINGERPRINT}\";\n")
endfunction()
