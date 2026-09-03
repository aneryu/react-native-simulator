# Open addon catalog. Any runtime/addons/<name> or RNS_ADDON_DIRS entry may
# declare BUILTIN/MODULE; duplicate keys fail configure.

set(RNS_ADDON_DIRS "" CACHE STRING
  "Semicolon-separated extra addon directories (each contains <name>/CMakeLists.txt)")

set_property(GLOBAL PROPERTY RNS_DECLARED_ADDON_KEYS "")
set_property(GLOBAL PROPERTY RNS_BUILTIN_ADDON_ROWS "")

function(rns_addon_fail message)
  message(FATAL_ERROR "${message}")
endfunction()

function(rns_enable_addon_module_host target)
  if(APPLE)
    if(NOT RNS_ENABLE_SANITIZERS)
      target_link_options(${target} PRIVATE
        "-Wl,-exported_symbols_list,${PROJECT_SOURCE_DIR}/cmake/macos-engine-exported-symbols.txt")
      set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        "${PROJECT_SOURCE_DIR}/cmake/macos-engine-exported-symbols.txt")
    endif()
  else()
    target_link_options(${target} PRIVATE "-rdynamic")
  endif()
endfunction()

function(rns_addon_common_includes target)
  target_include_directories(${target} PRIVATE
    "${PROJECT_SOURCE_DIR}/runtime/include"
    "${RNS_RUNTIME_GENERATED_DIR}"
    "${RN_ROOT}/ReactCommon"
    "${RN_ROOT}/ReactCommon/callinvoker"
    "${RN_ROOT}/ReactCommon/runtimeexecutor"
    "${RN_ROOT}/ReactCommon/react/nativemodule/core"
    "${RN_ROOT}/ReactCommon/jsi"
    "${RN_ROOT}/ReactCommon/react/renderer/components/view/platform/android"
    "${RN_ROOT}/ReactCommon/react/renderer/components/view/platform/cxx"
    "${RN_ROOT}/ReactCommon/react/renderer/graphics/platform/cxx"
    "${RN_ROOT}/ReactCommon/yoga"
    "${PROJECT_SOURCE_DIR}/runtime/shims/graphics-cxx"
    "${HERMES_SOURCE}/API"
    "${HERMES_SOURCE}/API/jsi"
    "${FOLLY_INCLUDE_DIR}")
endfunction()

function(rns_declare_addon)
  cmake_parse_arguments(ARG
    "BUILTIN;MODULE;TEST_ONLY"
    "NAME;AUTO;INSTALL_COMPONENT;FACTORY"
    "SOURCES"
    ${ARGN})
  if(NOT ARG_NAME)
    rns_addon_fail("rns_declare_addon requires NAME")
  endif()
  if(NOT ARG_SOURCES)
    rns_addon_fail("rns_declare_addon ${ARG_NAME} requires SOURCES")
  endif()
  if(NOT ARG_FACTORY)
    rns_addon_fail("rns_declare_addon ${ARG_NAME} requires FACTORY")
  endif()
  if(ARG_BUILTIN AND NOT ARG_AUTO)
    rns_addon_fail("BUILTIN addon ${ARG_NAME} requires AUTO always|expo|never")
  endif()
  if(ARG_AUTO AND NOT ARG_AUTO MATCHES "^(always|expo|never)$")
    rns_addon_fail("addon ${ARG_NAME} AUTO must be always, expo, or never")
  endif()
  # TEST_ONLY applies to the MODULE artifact (never catalogued as a path
  # install). Built-ins may still ship a tests-only MODULE copy.

  get_property(_keys GLOBAL PROPERTY RNS_DECLARED_ADDON_KEYS)
  if(ARG_NAME IN_LIST _keys)
    rns_addon_fail("Duplicate addon catalog key: ${ARG_NAME}")
  endif()
  list(APPEND _keys "${ARG_NAME}")
  set_property(GLOBAL PROPERTY RNS_DECLARED_ADDON_KEYS "${_keys}")

  set(_safe_name "${ARG_NAME}")
  string(REPLACE "-" "_" _safe_name "${_safe_name}")
  set(_object react-native-simulator-addon-${_safe_name}-objects)
  add_library(${_object} OBJECT ${ARG_SOURCES})
  set_target_properties(${_object} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
  target_compile_options(${_object} PRIVATE
    -fvisibility=hidden -fvisibility-inlines-hidden
    -Wall -Wextra -Wpedantic)
  target_compile_definitions(${_object} PRIVATE
    RNS_REACT_NATIVE_VERSION="${RNS_REACT_NATIVE_VERSION}"
    RNS_HERMES_VERSION="${RNS_HERMES_VERSION}"
    RNS_ADDON_API_FINGERPRINT="${RNS_ADDON_API_FINGERPRINT}")
  rns_addon_common_includes(${_object})
  target_link_libraries(${_object} PRIVATE
    rns_folly_headers glog::glog FastFloat::fast_float)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${_object} PRIVATE -Wno-changes-meaning)
  endif()
  if(RNS_ENABLE_SANITIZERS)
    target_compile_options(${_object} PRIVATE
      -fsanitize=address,undefined -fno-sanitize=vptr
      -fno-omit-frame-pointer)
  endif()

  if(ARG_BUILTIN)
    target_sources(react-native-simulator-engine PRIVATE
      $<TARGET_OBJECTS:${_object}>)
    get_property(_rows GLOBAL PROPERTY RNS_BUILTIN_ADDON_ROWS)
    list(APPEND _rows "${ARG_NAME}|${ARG_AUTO}|${ARG_FACTORY}")
    set_property(GLOBAL PROPERTY RNS_BUILTIN_ADDON_ROWS "${_rows}")
  endif()

  if(ARG_MODULE)
    if(APPLE)
      set(_suffix ".dylib")
      set(_build_rpath "@loader_path")
      set(_install_rpath "@loader_path/..")
    else()
      set(_suffix ".so")
      set(_build_rpath "\$ORIGIN")
      set(_install_rpath "\$ORIGIN/..")
    endif()
    set(_module react-native-simulator-addon-${_safe_name})
    set(_entry "${CMAKE_CURRENT_BINARY_DIR}/${_safe_name}_module_entry.cpp")
    file(WRITE "${_entry}"
"#include <react-native-simulator/SimulatorAddon.h>
#include \"AddonApiFingerprint.h\"
#include <memory>

std::unique_ptr<ReactNativeSimulator::SimulatorAddon> ${ARG_FACTORY}();

extern \"C\" RNS_EXPORT const ReactNativeSimulator::SimulatorAddonDescriptor*
react_native_simulator_addon_v4() noexcept {
  using namespace ReactNativeSimulator;
  static const SimulatorAddonDescriptor descriptor{
      sizeof(SimulatorAddonDescriptor),
      kSimulatorAddonAbiVersion,
      kSimulatorAddonApiFingerprint,
      \"${ARG_NAME}\",
      RNS_REACT_NATIVE_VERSION,
      RNS_HERMES_VERSION,
      []() -> SimulatorAddon* { return ${ARG_FACTORY}().release(); },
      [](SimulatorAddon* addon) noexcept { delete addon; },
  };
  return &descriptor;
}
")
    add_library(${_module} MODULE
      "${_entry}"
      $<TARGET_OBJECTS:${_object}>)
    set_target_properties(${_module} PROPERTIES
      PREFIX ""
      OUTPUT_NAME "rns-addon-${ARG_NAME}"
      SUFFIX "${_suffix}"
      BUILD_RPATH "${_build_rpath}"
      INSTALL_RPATH "${_install_rpath}"
      CXX_VISIBILITY_PRESET hidden
      C_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON
      EXCLUDE_FROM_ALL TRUE)
    target_compile_definitions(${_module} PRIVATE
      RNS_REACT_NATIVE_VERSION="${RNS_REACT_NATIVE_VERSION}"
      RNS_HERMES_VERSION="${RNS_HERMES_VERSION}"
      RNS_ADDON_API_FINGERPRINT="${RNS_ADDON_API_FINGERPRINT}")
    rns_addon_common_includes(${_module})
    target_link_libraries(${_module} PRIVATE
      rns_folly_headers glog::glog FastFloat::fast_float)
    if(APPLE)
      target_link_options(${_module} PRIVATE
        "-Wl,-undefined,dynamic_lookup"
        "$<$<CONFIG:Release>:-Wl,-dead_strip>")
    else()
      target_link_options(${_module} PRIVATE
        "$<$<CONFIG:Release>:-Wl,--gc-sections>")
    endif()
    target_compile_options(${_module} PRIVATE
      -fvisibility=hidden -fvisibility-inlines-hidden
      -Wall -Wextra -Wpedantic)
    if(RNS_ENABLE_SANITIZERS)
      target_compile_options(${_module} PRIVATE
        -fsanitize=address,undefined -fno-sanitize=vptr
        -fno-omit-frame-pointer)
      target_link_options(${_module} PRIVATE -fsanitize=address,undefined)
    endif()
    if(ARG_INSTALL_COMPONENT AND NOT ARG_TEST_ONLY)
      install(TARGETS ${_module}
        LIBRARY DESTINATION .
          COMPONENT ${ARG_INSTALL_COMPONENT}
          EXCLUDE_FROM_ALL)
    elseif(NOT ARG_TEST_ONLY)
      install(TARGETS ${_module}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/react-native-simulator/addons
          COMPONENT addon-development
          EXCLUDE_FROM_ALL)
    endif()
  endif()
endfunction()

function(rns_generate_builtin_addon_catalog)
  get_property(_rows GLOBAL PROPERTY RNS_BUILTIN_ADDON_ROWS)
  set(_body
"#include \"BuiltinAddonCatalog.h\"\n\n")
  foreach(_row IN LISTS _rows)
    string(REPLACE "|" ";" _parts "${_row}")
    list(GET _parts 2 _factory)
    string(APPEND _body
      "std::unique_ptr<ReactNativeSimulator::SimulatorAddon> ${_factory}();\n")
  endforeach()
  string(APPEND _body "\nnamespace ReactNativeSimulator {\n")
  foreach(_row IN LISTS _rows)
    string(REPLACE "|" ";" _parts "${_row}")
    list(GET _parts 2 _factory)
    string(APPEND _body
      "static std::unique_ptr<SimulatorAddon> (*const kKeep_${_factory})() = &${_factory};\n")
  endforeach()
  string(APPEND _body
"const std::vector<BuiltinAddonCatalogEntry>& builtinAddonCatalog() {\n"
"  static const std::vector<BuiltinAddonCatalogEntry> catalog{\n")
  foreach(_row IN LISTS _rows)
    string(REPLACE "|" ";" _parts "${_row}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _auto)
    list(GET _parts 2 _factory)
    if(_auto STREQUAL "always")
      set(_policy "AddonAutoPolicy::Always")
    elseif(_auto STREQUAL "expo")
      set(_policy "AddonAutoPolicy::Expo")
    else()
      set(_policy "AddonAutoPolicy::Never")
    endif()
    string(APPEND _body
      "      {\"${_name}\", ${_policy}, kKeep_${_factory}},\n")
  endforeach()
  string(APPEND _body
"  };\n  return catalog;\n}\n}\n")
  file(WRITE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/BuiltinAddonCatalog.cpp"
    "${_body}")
endfunction()

function(rns_discover_and_add_addons)
  set(_in_tree "${CMAKE_CURRENT_LIST_DIR}/addons")
  file(GLOB _addon_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS "${_in_tree}/*")
  list(SORT _addon_dirs)
  foreach(_dir IN LISTS _addon_dirs)
    if(NOT IS_DIRECTORY "${_dir}")
      continue()
    endif()
    if(NOT EXISTS "${_dir}/CMakeLists.txt")
      file(GLOB _contents "${_dir}/*")
      if(_contents)
        rns_addon_fail(
          "Addon directory ${_dir} has sources but no CMakeLists.txt")
      endif()
    endif()
  endforeach()

  set(_upstream expo safe-area compat-rn73)
  foreach(_name IN LISTS _upstream)
    if(EXISTS "${_in_tree}/${_name}/CMakeLists.txt")
      add_subdirectory("${_in_tree}/${_name}")
    endif()
  endforeach()
  file(GLOB _cmake_files CONFIGURE_DEPENDS "${_in_tree}/*/CMakeLists.txt")
  list(SORT _cmake_files)
  foreach(_file IN LISTS _cmake_files)
    get_filename_component(_dir "${_file}" DIRECTORY)
    get_filename_component(_name "${_dir}" NAME)
    if(_name IN_LIST _upstream)
      continue()
    endif()
    add_subdirectory("${_dir}")
  endforeach()

  foreach(_extra IN LISTS RNS_ADDON_DIRS)
    if(_extra STREQUAL "")
      continue()
    endif()
    if(NOT IS_DIRECTORY "${_extra}")
      rns_addon_fail("RNS_ADDON_DIRS entry is not a directory: ${_extra}")
    endif()
    file(GLOB _extra_cmake CONFIGURE_DEPENDS "${_extra}/*/CMakeLists.txt")
    list(SORT _extra_cmake)
    file(GLOB _extra_dirs LIST_DIRECTORIES true "${_extra}/*")
    foreach(_dir IN LISTS _extra_dirs)
      if(IS_DIRECTORY "${_dir}" AND NOT EXISTS "${_dir}/CMakeLists.txt")
        file(GLOB _contents "${_dir}/*")
        if(_contents)
          rns_addon_fail(
            "Addon directory ${_dir} has sources but no CMakeLists.txt")
        endif()
      endif()
    endforeach()
    foreach(_file IN LISTS _extra_cmake)
      get_filename_component(_dir "${_file}" DIRECTORY)
      get_filename_component(_extra_name "${_dir}" NAME)
      add_subdirectory("${_dir}"
        "${CMAKE_CURRENT_BINARY_DIR}/external-addons/${_extra_name}")
    endforeach()
  endforeach()
endfunction()
