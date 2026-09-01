cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(RNS_IMGUI_DIR "${PROJECT_ROOT}/third_party/imgui")
if(NOT DEFINED RN_ROOT OR NOT DEFINED HERMES_SOURCE OR
   NOT DEFINED HERMES_BINARY_DIR)
  message(FATAL_ERROR
    "Configure from the repository root: cmake -S . -B build")
endif()
find_path(FOLLY_INCLUDE_DIR folly/dynamic.h
  PATHS /opt/homebrew/include /usr/local/include)
if(NOT FOLLY_INCLUDE_DIR)
  message(FATAL_ERROR "folly headers are required (for example: brew install folly).")
endif()

find_package(glog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_file(FOLLY_STATIC_LIBRARY NAMES libfolly.a REQUIRED
  PATHS /opt/homebrew/lib /usr/local/lib)
find_library(DOUBLE_CONVERSION_LIBRARY NAMES double-conversion REQUIRED
  PATHS /opt/homebrew/lib /usr/local/lib)

# Homebrew's shared Folly records broad formula dependencies that this runtime
# does not use, including Boost.Regex and its ICU dylibs. Link the matching
# static archive so the linker extracts only referenced Folly objects. Keep its
# actual link requirements target-scoped; consumers of our intermediate static
# libraries need these dependencies too. ld64's load_hidden keeps Folly's
# implementation and weak libc++ template symbols from interposing on Hermes.
add_library(rns_folly INTERFACE)
target_include_directories(rns_folly INTERFACE "${FOLLY_INCLUDE_DIR}")
target_link_libraries(rns_folly INTERFACE
  "-Wl,-load_hidden,${FOLLY_STATIC_LIBRARY}"
  glog::glog
  fmt::fmt)

foreach(required_path
    "${HERMES_SOURCE}/API/hermes/hermes.h"
    "${RN_ROOT}/ReactCommon/react/runtime/ReactInstance.cpp")
  if(NOT EXISTS "${required_path}")
    message(FATAL_ERROR "Missing runtime source: ${required_path}")
  endif()
endforeach()
if(NOT TARGET hermesvm_a OR NOT TARGET jsi OR NOT TARGET FastFloat::fast_float)
  message(FATAL_ERROR
    "Hermes, JSI, and fast_float targets must be configured by the root project.")
endif()

set(RUNTIME_SCHEDULER_DIR "${RN_ROOT}/ReactCommon/react/renderer/runtimescheduler")

# GitHub source checkouts do not contain the npm package's generated
# FBReactNativeSpecJSI.h. Generate the small method map needed by the headless
# feature-flag TurboModule directly from RN's checked-in C++ declarations.
set(FEATURE_FLAGS_HEADER
  "${RN_ROOT}/ReactCommon/react/nativemodule/featureflags/NativeReactNativeFeatureFlags.h")
file(STRINGS "${FEATURE_FLAGS_HEADER}" FEATURE_FLAG_DECLARATIONS
  REGEX "^[ \t]+(bool|double) [A-Za-z0-9_]+\\(")
set(FEATURE_FLAGS_METHOD_MAP "")
foreach(declaration IN LISTS FEATURE_FLAG_DECLARATIONS)
  string(REGEX REPLACE
    ".*(bool|double) ([A-Za-z0-9_]+)\\(.*" "\\1"
    return_type "${declaration}")
  string(REGEX REPLACE
    ".*(bool|double) ([A-Za-z0-9_]+)\\(.*" "\\2"
    method_name "${declaration}")
  if(method_name STREQUAL "commonTestFlagWithoutNativeImplementation")
    set(return_expression "false")
  else()
    set(return_expression
      "react::ReactNativeFeatureFlags::${method_name}()")
  endif()
  string(APPEND FEATURE_FLAGS_METHOD_MAP
    "    methodMap_[\"${method_name}\"] = {0, [](jsi::Runtime&, react::TurboModule&, const jsi::Value*, size_t) -> jsi::Value { return ${return_expression}; }};\n")
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(WRITE
  "${CMAKE_CURRENT_BINARY_DIR}/generated/HeadlessFeatureFlagsMethodMap.inc"
  "${FEATURE_FLAGS_METHOD_MAP}")

file(GLOB FABRIC_SOURCES CONFIGURE_DEPENDS
  "${RN_ROOT}/ReactCommon/logger/*.cpp"
  "${RN_ROOT}/ReactCommon/react/debug/*.cpp"
  "${RN_ROOT}/ReactCommon/react/featureflags/*.cpp"
  "${RN_ROOT}/ReactCommon/react/utils/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/componentregistry/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/core/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/css/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/debug/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/consistency/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/dom/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/graphics/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/graphics/platform/cxx/react/renderer/graphics/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/mapbuffer/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/telemetry/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/components/root/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/components/view/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/components/view/platform/android/react/renderer/components/view/HostPlatformViewProps.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/components/view/platform/cxx/react/renderer/components/view/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/attributedstring/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/components/text/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/components/textinput/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/components/textinput/platform/ios/react/renderer/components/iostextinput/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/components/scrollview/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/components/scrollview/platform/android/react/renderer/components/scrollview/AndroidHorizontalScrollContentViewShadowNode.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/components/image/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/imagemanager/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/imagemanager/platform/cxx/react/renderer/imagemanager/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/textlayoutmanager/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/mounting/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/mounting/internal/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/leakchecker/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/uimanager/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/uimanager/consistency/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/animations/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/observers/intersection/IntersectionObserver.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/observers/intersection/IntersectionObserverManager.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/observers/intersection/IntersectionObserverState.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/observers/mutation/MutationObserver.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/observers/mutation/MutationObserverManager.cpp"
)
list(FILTER FABRIC_SOURCES EXCLUDE REGEX "/mounting/internal/CullingContext\\.cpp$")
list(FILTER FABRIC_SOURCES EXCLUDE REGEX "/animations/tests/")
list(FILTER FABRIC_SOURCES EXCLUDE REGEX
  "/components/text/(BaseParagraphProps|ParagraphShadowNode)\\.cpp$")
list(APPEND FABRIC_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/shims/react-native/react/renderer/components/text/BaseParagraphProps.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/shims/react-native/react/renderer/components/text/ParagraphShadowNode.cpp")

file(GLOB ANIMATED_SOURCES CONFIGURE_DEPENDS
  "${RN_ROOT}/ReactCommon/react/renderer/animated/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/animated/drivers/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/animated/event_drivers/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/animated/nodes/*.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/animationbackend/*.cpp")
list(FILTER ANIMATED_SOURCES EXCLUDE REGEX "/animated/tests/")
list(FILTER ANIMATED_SOURCES EXCLUDE REGEX "/AnimatedModule\\.cpp$")
list(FILTER ANIMATED_SOURCES EXCLUDE REGEX "/NativeAnimatedNodesManagerProvider\\.cpp$")
list(FILTER ANIMATED_SOURCES EXCLUDE REGEX "/AnimatedMountingOverrideDelegate\\.cpp$")

file(GLOB JS_ERROR_HANDLER_SOURCES CONFIGURE_DEPENDS
  "${RN_ROOT}/ReactCommon/jserrorhandler/*.cpp")
file(GLOB REACT_PERFORMANCE_SOURCES CONFIGURE_DEPENDS
  "${RN_ROOT}/ReactCommon/react/performance/timeline/*.cpp")
file(GLOB REACT_PERF_LOGGER_SOURCES CONFIGURE_DEPENDS
  "${RN_ROOT}/ReactCommon/reactperflogger/reactperflogger/*.cpp")
file(GLOB JS_INSPECTOR_SOURCES CONFIGURE_DEPENDS
  "${RN_ROOT}/ReactCommon/jsinspector-modern/*.cpp"
  "${RN_ROOT}/ReactCommon/jsinspector-modern/cdp/*.cpp"
  "${RN_ROOT}/ReactCommon/jsinspector-modern/network/*.cpp"
  "${RN_ROOT}/ReactCommon/jsinspector-modern/tracing/*.cpp")
file(GLOB HERMES_INSPECTOR_SOURCES CONFIGURE_DEPENDS
  "${RN_ROOT}/ReactCommon/hermes/inspector-modern/chrome/*.cpp")

add_subdirectory("${RN_ROOT}/ReactCommon/yoga/yoga" "${CMAKE_BINARY_DIR}/yoga")

add_library(react-native-simulator-engine SHARED
  src/core/SceneHitTest.cpp
  src/core/SimulatorAddon.cpp
  src/core/SimulatorEventLoop.cpp
  src/core/HostEnvironment.cpp
  src/core/HttpBundleLoader.cpp
  src/core/PackagerConnection.mm
  src/core/SimulatorEngine.cpp
  src/core/react_native_simulator_shims.cpp
  src/devtools/InspectorTransport.cpp
  src/devtools/DevToolsHost.cpp
  src/devtools/HostChrome.cpp
  src/devtools/HostUi.cpp
  src/fabric/HeadlessFabric.cpp
  src/fabric/HeadlessReactFabric.cpp
  src/fabric/HeadlessSampleComponent.cpp
  src/fabric/HeadlessOfficialComponents.cpp
  src/fabric/HeadlessTextLayoutManager.cpp
  src/fabric/HeadlessTextInputComponent.cpp
  src/fabric/HeadlessNativeDOM.cpp
  src/modules/HeadlessRNModules.cpp
  src/modules/HeadlessTurboModules.cpp
  src/modules/HeadlessAnimatedModule.cpp
  src/modules/HeadlessImageAssets.cpp
  src/modules/HeadlessImageManager.cpp
  src/modules/HeadlessHttp.mm
  src/modules/HeadlessBlob.cpp
  src/modules/HeadlessWebSocket.mm
  src/modules/HeadlessImageEditing.mm
  src/modules/HeadlessIosModules.cpp
  src/modules/HeadlessKeyboard.cpp
  src/modules/HeadlessBackPress.cpp
  src/modules/HeadlessThinModules.cpp
  src/modules/HeadlessObservers.cpp
  src/profiles/RuntimeProfile.cpp
  "${RUNTIME_SCHEDULER_DIR}/RuntimeScheduler.cpp"
  "${RUNTIME_SCHEDULER_DIR}/RuntimeScheduler_Modern.cpp"
  "${RUNTIME_SCHEDULER_DIR}/RuntimeSchedulerBinding.cpp"
  "${RUNTIME_SCHEDULER_DIR}/Task.cpp"
  "${RUNTIME_SCHEDULER_DIR}/RuntimeSchedulerCallInvoker.cpp"
  "${RN_ROOT}/ReactCommon/react/runtime/BufferedRuntimeExecutor.cpp"
  "${RN_ROOT}/ReactCommon/react/runtime/ReactInstance.cpp"
  "${RN_ROOT}/ReactCommon/react/runtime/TimerManager.cpp"
  "${RN_ROOT}/ReactCommon/jsitooling/react/runtime/JSRuntimeBindings.cpp"
  "${RN_ROOT}/ReactCommon/jsitooling/react/runtime/JSRuntimeFactory.cpp"
  "${RN_ROOT}/ReactCommon/cxxreact/JSBigString.cpp"
  "${RN_ROOT}/ReactCommon/cxxreact/ReactMarker.cpp"
  "${RN_ROOT}/ReactCommon/oscompat/OSCompatPosix.cpp"
  "${RN_ROOT}/ReactCommon/react/bridging/LongLivedObject.cpp"
  "${RN_ROOT}/ReactCommon/react/nativemodule/core/ReactCommon/TurboModule.cpp"
  "${RN_ROOT}/ReactCommon/react/nativemodule/core/ReactCommon/TurboModuleBinding.cpp"
  "${RN_ROOT}/ReactCommon/react/nativemodule/core/ReactCommon/TurboModuleUtils.cpp"
  "${RN_ROOT}/ReactCommon/react/nativemodule/core/ReactCommon/TurboModuleWithJSIBindings.cpp"
  "${RN_ROOT}/ReactCommon/jsinspector-modern/tracing/EventLoopReporter.cpp"
  "${RN_ROOT}/ReactCxxPlatform/react/threading/TaskDispatchThread.cpp"
  "${RN_ROOT}/ReactCommon/jsi/jsi/JSIDynamic.cpp"
  "${RN_ROOT}/ReactCommon/runtimeexecutor/platform/cxx/ReactCommon/RuntimeExecutorSyncUIThreadUtils.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/element/ComponentBuilder.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/components/unimplementedview/UnimplementedViewComponentDescriptor.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/components/unimplementedview/UnimplementedViewProps.cpp"
  "${RN_ROOT}/ReactCommon/react/renderer/components/unimplementedview/UnimplementedViewShadowNode.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/shims/react-native/react/renderer/textlayoutmanager/TextLayoutManager.cpp"
  ${JS_ERROR_HANDLER_SOURCES}
  ${REACT_PERFORMANCE_SOURCES}
  ${REACT_PERF_LOGGER_SOURCES}
  ${JS_INSPECTOR_SOURCES}
  ${HERMES_INSPECTOR_SOURCES}
  ${FABRIC_SOURCES}
  ${ANIMATED_SOURCES}
)
add_library(ReactNativeSimulator::Engine ALIAS react-native-simulator-engine)
set_source_files_properties(src/modules/HeadlessHttp.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc")
set_source_files_properties(src/modules/HeadlessImageEditing.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc")
set_source_files_properties(src/modules/HeadlessWebSocket.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc")
set_source_files_properties(src/core/PackagerConnection.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc")
set_target_properties(react-native-simulator-engine PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION 0
  EXPORT_NAME Engine)

target_compile_definitions(react-native-simulator-engine PRIVATE
  RCT_REMOVE_LEGACY_ARCH=1
  RCT_REMOVE_LEGACY_COMPONENT_INTEROP=1
  HERMES_ENABLE_DEBUGGER=1
  REACT_NATIVE_DEBUGGER_ENABLED=1
  REACT_NATIVE_DEBUGGER_ENABLED_DEVONLY=1
  RNS_REACT_NATIVE_VERSION="${RNS_REACT_NATIVE_VERSION}"
  RNS_REACT_PEER_RANGE="${RNS_REACT_PEER_RANGE}"
  RNS_HERMES_VERSION="${RNS_HERMES_VERSION}")
if(RNS_ENABLE_SOURCE_DEVTOOLS_FRONTEND)
  target_compile_definitions(react-native-simulator-engine PRIVATE
    RNS_ENABLE_SOURCE_DEVTOOLS_FRONTEND=1
    RNS_DEBUGGER_FRONTEND_DIR="${RN_ROOT}/../debugger-frontend/dist/third-party/front_end")
else()
  target_compile_definitions(react-native-simulator-engine PRIVATE
    RNS_ENABLE_SOURCE_DEVTOOLS_FRONTEND=0)
endif()
target_include_directories(react-native-simulator-engine
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
  PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
  "${CMAKE_CURRENT_LIST_DIR}/src/core"
  "${CMAKE_CURRENT_LIST_DIR}/src/devtools"
  "${CMAKE_CURRENT_LIST_DIR}/src/fabric"
  "${CMAKE_CURRENT_LIST_DIR}/src/generated"
  "${CMAKE_CURRENT_LIST_DIR}/src/modules"
  "${CMAKE_CURRENT_LIST_DIR}/src/profiles"
  "${CMAKE_CURRENT_LIST_DIR}/shims/react-native"
  "${RN_ROOT}/ReactCommon"
  "${RN_ROOT}/ReactCommon/callinvoker"
  "${RN_ROOT}/ReactCommon/runtimeexecutor"
  "${RN_ROOT}/ReactCommon/runtimeexecutor/platform/cxx"
  "${RN_ROOT}/ReactCommon/jsitooling"
  "${RN_ROOT}/ReactCommon/reactperflogger"
  "${RN_ROOT}/ReactCommon/react/nativemodule/core"
  "${RN_ROOT}/React"
  "${RN_ROOT}/ReactCommon/react/renderer/components/view/platform/android"
  "${RN_ROOT}/ReactCommon/react/renderer/components/view/platform/cxx"
  "${RN_ROOT}/ReactCommon/react/renderer/components/text/platform/cxx"
  "${RN_ROOT}/ReactCommon/react/renderer/components/textinput/platform/android/react/renderer/components"
  "${RN_ROOT}/ReactCommon/react/renderer/components/textinput/platform/ios/react/renderer/components"
  "${RN_ROOT}/ReactCommon/react/renderer/components/textinput/platform/ios"
  "${RN_ROOT}/ReactCommon/react/renderer/components/scrollview/platform/cxx"
  "${RN_ROOT}/ReactCommon/react/renderer/components/scrollview/platform/android"
  "${RN_ROOT}/ReactCommon/react/renderer/imagemanager"
  "${RN_ROOT}/ReactCommon/react/renderer/imagemanager/platform/cxx"
  "${RN_ROOT}/ReactCommon/react/renderer/textlayoutmanager/platform/cxx"
  "${CMAKE_CURRENT_LIST_DIR}/shims/graphics-cxx"
  "${RN_ROOT}/ReactCommon/react/renderer/graphics/platform/cxx"
  "${RN_ROOT}/ReactCommon/react/utils/platform/cxx"
  "${RN_ROOT}/ReactCxxPlatform"
  "${HERMES_SOURCE}/API"
  "${HERMES_SOURCE}/API/jsi"
  "${HERMES_SOURCE}/public"
  "${HERMES_SOURCE}/include"
  "${HERMES_BINARY_DIR}/include"
  "${HERMES_BINARY_DIR}/lib/config"
  "${FOLLY_INCLUDE_DIR}"
)

target_link_libraries(react-native-simulator-engine PRIVATE
  hermesvm_a
  yogacore
  Boost::boost
  rns_folly
  "${DOUBLE_CONVERSION_LIBRARY}"
  glog::glog
  fmt::fmt
  FastFloat::fast_float
  Threads::Threads
  "-framework CoreFoundation"
  "-framework Foundation"
  "-framework AppKit"
  "-framework CoreGraphics"
  "-framework ImageIO"
)

target_compile_options(
  react-native-simulator-engine PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(react-native-simulator-engine PRIVATE
  # Export only the installed Engine API, the JSI/TurboModule surface promised
  # by SimulatorAddon.h, and the separate in-tree CLI's host hooks. This keeps
  # Skia/ICU/Folly/Hermes implementation symbols out of the dylib ABI and lets
  # ld64 dead-strip code that accidental exports previously kept alive.
  "-Wl,-exported_symbols_list,${PROJECT_ROOT}/cmake/macos-engine-exported-symbols.txt"
  "$<$<CONFIG:Release>:-Wl,-dead_strip>")
set_property(TARGET react-native-simulator-engine APPEND PROPERTY LINK_DEPENDS
  "${PROJECT_ROOT}/cmake/macos-engine-exported-symbols.txt")

# AppleClang 17's vptr sanitizer reports invalid std::stringbuf vptrs while
# crossing the macOS system libc++ boundary, before simulator application code
# runs. Keep this disabled consistently for the engine, addons, and CLI; ASan
# and every other UBSan check remain enabled.
if(RNS_ENABLE_SANITIZERS)
  target_compile_options(react-native-simulator-engine PRIVATE
    -fsanitize=address,undefined -fno-sanitize=vptr
    -fno-omit-frame-pointer)
  target_link_options(react-native-simulator-engine PUBLIC
    -fsanitize=address,undefined)
endif()

function(rns_add_simulator_addon target_name output_name source)
  add_library(${target_name} MODULE ${source})
  set_target_properties(${target_name} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${output_name}"
    SUFFIX ".dylib"
    BUILD_RPATH "@loader_path"
    INSTALL_RPATH "@loader_path/..")
  target_compile_definitions(${target_name} PRIVATE
    RNS_REACT_NATIVE_VERSION="${RNS_REACT_NATIVE_VERSION}"
    RNS_HERMES_VERSION="${RNS_HERMES_VERSION}")
  target_include_directories(${target_name} PRIVATE
    "${RN_ROOT}/ReactCommon"
    "${RN_ROOT}/ReactCommon/jsi"
    "${RN_ROOT}/ReactCommon/callinvoker"
    "${RN_ROOT}/ReactCommon/runtimeexecutor"
    "${RN_ROOT}/ReactCommon/react/nativemodule/core"
    "${FOLLY_INCLUDE_DIR}")
  target_link_libraries(${target_name} PRIVATE react-native-simulator-engine)
  target_link_options(${target_name} PRIVATE
    "$<$<CONFIG:Release>:-Wl,-dead_strip>")
  target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
  if(RNS_ENABLE_SANITIZERS)
    target_compile_options(${target_name} PRIVATE
      -fsanitize=address,undefined -fno-sanitize=vptr
      -fno-omit-frame-pointer)
    target_link_options(${target_name} PRIVATE
      -fsanitize=address,undefined)
  endif()
endfunction()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/addons/shopee/ShopeeAddon.cpp")
  rns_add_simulator_addon(
    react-native-simulator-addon-shopee
    rns-addon-shopee
    addons/shopee/ShopeeAddon.cpp)
endif()
rns_add_simulator_addon(
  react-native-simulator-addon-rntester
  rns-addon-rntester
  addons/rntester/RNTesterAddon.cpp)
set_target_properties(react-native-simulator-addon-rntester PROPERTIES
  # The demo addon is installed next to its config and relies on the rnsim
  # executable's packaged engine rpath. It must not retain a build-tree path.
  INSTALL_RPATH "")

add_executable(react-native-simulator
  src/core/SimulatorConfig.cpp
  src/core/main.cpp)
set_target_properties(react-native-simulator PROPERTIES
  OUTPUT_NAME "rnsim"
  BUILD_RPATH "@loader_path"
  INSTALL_RPATH "@loader_path/../lib")
target_include_directories(react-native-simulator PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/src/core"
  "${RN_ROOT}/ReactCommon"
  "${RN_ROOT}/ReactCommon/callinvoker"
  "${RN_ROOT}/ReactCommon/react/nativemodule/core"
  "${HERMES_SOURCE}/API"
  "${HERMES_SOURCE}/API/jsi"
  "${FOLLY_INCLUDE_DIR}")
target_link_libraries(react-native-simulator PRIVATE
  react-native-simulator-engine
  Boost::boost
  rns_folly
  "${DOUBLE_CONVERSION_LIBRARY}")
target_link_options(react-native-simulator PRIVATE
  "$<$<CONFIG:Release>:-Wl,-dead_strip>")
target_compile_definitions(react-native-simulator PRIVATE
  RNS_PROJECT_VERSION="${PROJECT_VERSION}"
  RNS_REACT_NATIVE_VERSION="${RNS_REACT_NATIVE_VERSION}"
  RNS_HERMES_VERSION="${RNS_HERMES_VERSION}"
  RNS_BUILD_COMMIT="${RNS_BUILD_COMMIT}"
  RNS_BUILD_DIRTY="${RNS_BUILD_DIRTY}"
  RNS_BUILD_MIN_MACOS="${CMAKE_OSX_DEPLOYMENT_TARGET}")
target_compile_options(react-native-simulator PRIVATE -Wall -Wextra -Wpedantic)
if(RNS_ENABLE_SANITIZERS)
  target_compile_options(react-native-simulator PRIVATE
    -fsanitize=address,undefined -fno-sanitize=vptr
    -fno-omit-frame-pointer)
  target_link_options(react-native-simulator PRIVATE -fsanitize=address,undefined)
endif()

if(RNS_ENABLE_SKIA)
  add_subdirectory("${PROJECT_ROOT}/render" "${CMAKE_BINARY_DIR}/render")

  target_compile_definitions(react-native-simulator-engine PRIVATE
    RNS_ENABLE_SKIA=1)
  target_link_libraries(react-native-simulator-engine PRIVATE
    react-native-simulator-skia-text)

  target_compile_definitions(react-native-simulator PRIVATE RNS_ENABLE_SKIA=1)
  target_link_libraries(react-native-simulator PRIVATE
    react-native-simulator-skia-renderer
    "${DOUBLE_CONVERSION_LIBRARY}")
endif()

if(RNS_ENABLE_IMGUI)
  add_subdirectory("${PROJECT_ROOT}/frontend" "${CMAKE_BINARY_DIR}/frontend")

  if(RNS_ENABLE_SKIA)
    target_sources(react-native-simulator PRIVATE
      "${PROJECT_ROOT}/frontend/InteractiveFrontend.cpp"
      "${PROJECT_ROOT}/frontend/MacWindowChrome.mm")
    set_source_files_properties(
      "${PROJECT_ROOT}/frontend/MacWindowChrome.mm" PROPERTIES
      COMPILE_FLAGS "-fobjc-arc")
    target_include_directories(react-native-simulator PRIVATE
      "${PROJECT_ROOT}/frontend"
      "${CMAKE_BINARY_DIR}/frontend"
      "${CMAKE_CURRENT_LIST_DIR}/src/devtools")
    target_compile_definitions(react-native-simulator PRIVATE
      RNS_ENABLE_IMGUI=1)
    target_link_libraries(react-native-simulator PRIVATE
      react-native-simulator-imgui
      "-framework AppKit")
    add_dependencies(react-native-simulator react-native-simulator-app-icon)
    add_custom_command(TARGET react-native-simulator POST_BUILD
      COMMAND osascript -l JavaScript
              "${PROJECT_ROOT}/tools/macos/set-file-icon.js"
              "${PROJECT_ROOT}/docs/assets/AppIcon.icns"
              "$<TARGET_FILE:react-native-simulator>"
      COMMENT "Setting rnsim file icon"
      VERBATIM)
  endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS react-native-simulator-engine react-native-simulator
  EXPORT ReactNativeSimulatorTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT react-native-simulator
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT react-native-simulator
    NAMELINK_COMPONENT react-native-simulator
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT react-native-simulator)
install(TARGETS react-native-simulator-addon-rntester
  LIBRARY DESTINATION .
    COMPONENT rntester-demo)
if(TARGET react-native-simulator-addon-shopee)
  install(TARGETS react-native-simulator-addon-shopee
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/react-native-simulator/addons
      COMPONENT react-native-simulator)
endif()
if(RNS_ENABLE_IMGUI)
  install(TARGETS react-native-simulator-inspector
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT react-native-simulator-imgui)
endif()
install(FILES
  "${CMAKE_CURRENT_LIST_DIR}/include/react-native-simulator/Engine.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/react-native-simulator/Interaction.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/react-native-simulator/Scene.h"
  "${CMAKE_CURRENT_LIST_DIR}/include/react-native-simulator/SimulatorAddon.h"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/react-native-simulator
  COMPONENT react-native-simulator)
install(DIRECTORY "${RN_ROOT}/../debugger-frontend/dist/third-party/front_end/"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/react-native-simulator/debugger-frontend
  COMPONENT react-native-simulator)
configure_package_config_file(
  "${PROJECT_ROOT}/cmake/ReactNativeSimulatorConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/ReactNativeSimulatorConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ReactNativeSimulator)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/ReactNativeSimulatorConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  # The embedding and addon ABI remain pre-1.0. Do not let a future 0.x minor
  # silently satisfy a consumer built against another experimental release.
  COMPATIBILITY ExactVersion)
install(EXPORT ReactNativeSimulatorTargets
  FILE ReactNativeSimulatorTargets.cmake
  NAMESPACE ReactNativeSimulator::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ReactNativeSimulator
  COMPONENT react-native-simulator)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/ReactNativeSimulatorConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/ReactNativeSimulatorConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ReactNativeSimulator
  COMPONENT react-native-simulator)
