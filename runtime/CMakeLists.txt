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

include("${PROJECT_ROOT}/cmake/GenerateAddonApiFingerprint.cmake")
include("${PROJECT_ROOT}/cmake/RnsAddon.cmake")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(RNS_RUNTIME_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
  CACHE INTERNAL "Generated runtime headers")
rns_generate_addon_api_fingerprint()

add_library(react-native-simulator-engine STATIC
  src/core/SceneHitTest.cpp
  src/core/SimulatorAddon.cpp
  src/core/LaunchPlan.cpp
  src/core/FrameworkInventory.cpp
  src/core/SimulatorEventLoop.cpp
  src/core/HostEnvironment.cpp
  src/core/HostPlatform.cpp
  src/core/HttpBundleLoader.cpp
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
  src/modules/ImagePixelSize.cpp
  src/modules/HeadlessBlob.cpp
  src/modules/HeadlessIosModules.cpp
  src/modules/HeadlessKeyboard.cpp
  src/modules/HeadlessBackPress.cpp
  src/modules/HeadlessThinModules.cpp
  src/modules/HeadlessObservers.cpp
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
if(APPLE)
  target_sources(react-native-simulator-engine PRIVATE
    src/core/PackagerConnection.mm
    src/modules/HeadlessHttp.mm
    src/modules/HeadlessWebSocket.mm
    src/modules/HeadlessImageEditing.mm)
  set_source_files_properties(
    src/core/PackagerConnection.mm
    src/modules/HeadlessHttp.mm
    src/modules/HeadlessWebSocket.mm
    src/modules/HeadlessImageEditing.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
  target_sources(react-native-simulator-engine PRIVATE
    src/core/PackagerConnection.cpp
    src/modules/HeadlessHttp.cpp
    src/modules/HeadlessWebSocket.cpp
    src/modules/HeadlessImageEditing.cpp
    src/modules/ImageBuffer.cpp)
endif()
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
  RNS_HERMES_VERSION="${RNS_HERMES_VERSION}"
  RNS_ADDON_API_FINGERPRINT="${RNS_ADDON_API_FINGERPRINT}")
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
    "$<BUILD_INTERFACE:${RN_ROOT}/ReactCommon>"
    "$<BUILD_INTERFACE:${RN_ROOT}/ReactCommon/callinvoker>"
    "$<BUILD_INTERFACE:${RN_ROOT}/ReactCommon/runtimeexecutor>"
    "$<BUILD_INTERFACE:${RN_ROOT}/ReactCommon/react/nativemodule/core>"
    "$<BUILD_INTERFACE:${RN_ROOT}/ReactCommon/jsi>"
    "$<BUILD_INTERFACE:${HERMES_SOURCE}/API>"
    "$<BUILD_INTERFACE:${HERMES_SOURCE}/API/jsi>"
    "$<BUILD_INTERFACE:${FOLLY_INCLUDE_DIR}>"
    "$<BUILD_INTERFACE:${RN_ROOT}/ReactCommon/react/renderer/graphics/platform/cxx>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/shims/graphics-cxx>"
    "$<BUILD_INTERFACE:${RN_ROOT}/ReactCommon/react/utils/platform/cxx>"
    "$<BUILD_INTERFACE:${RN_ROOT}/ReactCommon/react/renderer/components/view/platform/android>"
    "$<BUILD_INTERFACE:${RN_ROOT}/ReactCommon/react/renderer/components/view/platform/cxx>"
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
  Boost::boost
  FastFloat::fast_float
  Threads::Threads)
target_link_libraries(react-native-simulator-engine PUBLIC yogacore)
# Folly headers need FOLLY_NO_CONFIG on Linux. Keep this PUBLIC so the CLI,
# tests, and frontend inherit the same include path and compile definitions.
target_link_libraries(react-native-simulator-engine PUBLIC rns_folly)
if(APPLE)
  target_link_libraries(react-native-simulator-engine PRIVATE
    "-framework CoreFoundation"
    "-framework Foundation"
    "-framework AppKit"
    "-framework CoreGraphics"
    "-framework ImageIO")
else()
  target_link_libraries(react-native-simulator-engine PRIVATE
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    PNG::PNG
    ZLIB::ZLIB
    "${UUID_LIBRARY}"
    ${CMAKE_DL_LIBS}
    atomic)
endif()

target_compile_options(
  react-native-simulator-engine PRIVATE -Wall -Wextra -Wpedantic)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # RN ConcreteShadowNode::Props() collides with the nested Props type.
  target_compile_options(react-native-simulator-engine PRIVATE
    -Wno-changes-meaning)
endif()
if(NOT APPLE)
  target_compile_options(react-native-simulator-engine PRIVATE
    -include "${CMAKE_CURRENT_LIST_DIR}/shims/compat/ForceStdint.h")
endif()
if(RNS_ENABLE_SANITIZERS)
  target_compile_options(react-native-simulator-engine PRIVATE
    -fsanitize=address,undefined
    -fno-sanitize=vptr
    -fno-omit-frame-pointer)
  target_link_options(react-native-simulator-engine PUBLIC
    -fsanitize=address,undefined)
  if(NOT APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC discards ConcreteShadowNode typeinfo COMDATs for
    # LayoutConformanceShadowNode (internal-linkage component name) when
    # this TU is ASan/UBSan instrumented without vptr. Leave the file
    # uninstrumented so sanitized Linux can link; ABI fingerprint stays
    # address,undefined,no-vptr.
    set_source_files_properties(
      "${RN_ROOT}/ReactCommon/react/renderer/components/view/YogaLayoutableShadowNode.cpp"
      PROPERTIES COMPILE_OPTIONS "-fno-sanitize=address,undefined")
  endif()
endif()

rns_discover_and_add_addons()
rns_generate_builtin_addon_catalog()
target_sources(react-native-simulator-engine PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated/BuiltinAddonCatalog.cpp")

add_executable(react-native-simulator
  src/core/SimulatorConfig.cpp
  src/core/main.cpp)
if(APPLE)
  set(_rnsim_build_rpath "@loader_path")
  set(_rnsim_install_rpath "@loader_path/../lib")
else()
  set(_rnsim_build_rpath "\$ORIGIN")
  set(_rnsim_install_rpath "\$ORIGIN/../lib")
endif()
set_target_properties(react-native-simulator PROPERTIES
  OUTPUT_NAME "rnsim"
  BUILD_RPATH "${_rnsim_build_rpath}"
  INSTALL_RPATH "${_rnsim_install_rpath}")
target_include_directories(react-native-simulator PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/src/core"
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
  "${RN_ROOT}/ReactCommon"
  "${RN_ROOT}/ReactCommon/callinvoker"
  "${RN_ROOT}/ReactCommon/react/nativemodule/core"
  "${HERMES_SOURCE}/API"
  "${HERMES_SOURCE}/API/jsi"
  "${FOLLY_INCLUDE_DIR}")
target_link_libraries(react-native-simulator PRIVATE
  react-native-simulator-engine
  Boost::boost)
if(APPLE)
  target_link_options(react-native-simulator PRIVATE
    "$<$<CONFIG:Release>:-Wl,-dead_strip>")
endif()
rns_enable_addon_module_host(react-native-simulator)
if(APPLE)
  set(_rnsim_min_macos "${CMAKE_OSX_DEPLOYMENT_TARGET}")
else()
  set(_rnsim_min_macos "")
endif()
target_compile_definitions(react-native-simulator PRIVATE
  RNS_PROJECT_VERSION="${RNS_RELEASE_CHANNEL}"
  RNS_TRANSPORT_VERSION="${PROJECT_VERSION}"
  RNS_REACT_NATIVE_VERSION="${RNS_REACT_NATIVE_VERSION}"
  RNS_HERMES_VERSION="${RNS_HERMES_VERSION}"
  RNS_BUILD_COMMIT="${RNS_BUILD_COMMIT}"
  RNS_BUILD_DIRTY="${RNS_BUILD_DIRTY}"
  RNS_BUILD_MIN_MACOS="${_rnsim_min_macos}")
target_compile_options(react-native-simulator PRIVATE -Wall -Wextra -Wpedantic)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(react-native-simulator PRIVATE -Wno-changes-meaning)
endif()
if(RNS_ENABLE_SANITIZERS)
  target_compile_options(react-native-simulator PRIVATE
    -fsanitize=address,undefined
    -fno-sanitize=vptr
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
    react-native-simulator-skia-renderer)
endif()

if(RNS_ENABLE_IMGUI)
  add_subdirectory("${PROJECT_ROOT}/frontend" "${CMAKE_BINARY_DIR}/frontend")

  if(RNS_ENABLE_SKIA)
    if(APPLE)
      target_sources(react-native-simulator PRIVATE
        "${PROJECT_ROOT}/frontend/InteractiveFrontend.cpp"
        "${PROJECT_ROOT}/frontend/MacWindowChrome.mm")
      set_source_files_properties(
        "${PROJECT_ROOT}/frontend/MacWindowChrome.mm" PROPERTIES
        COMPILE_FLAGS "-fobjc-arc")
    else()
      target_sources(react-native-simulator PRIVATE
        "${PROJECT_ROOT}/frontend/InteractiveFrontend.cpp"
        "${PROJECT_ROOT}/frontend/HostWindowChrome.cpp")
    endif()
    target_include_directories(react-native-simulator PRIVATE
      "${PROJECT_ROOT}/frontend"
      "${CMAKE_BINARY_DIR}/frontend"
      "${CMAKE_CURRENT_LIST_DIR}/src/devtools")
    target_compile_definitions(react-native-simulator PRIVATE
      RNS_ENABLE_IMGUI=1)
    target_link_libraries(react-native-simulator PRIVATE
      react-native-simulator-imgui)
    if(APPLE)
      target_link_libraries(react-native-simulator PRIVATE "-framework AppKit")
      add_dependencies(react-native-simulator react-native-simulator-app-icon)
      add_custom_command(TARGET react-native-simulator POST_BUILD
        COMMAND osascript -l JavaScript
                "${PROJECT_ROOT}/tools/macos/set-file-icon.js"
                "${PROJECT_ROOT}/docs/assets/AppIcon.icns"
                "$<TARGET_FILE:react-native-simulator>"
        COMMENT "Setting rnsim file icon"
        VERBATIM)
    else()
      add_dependencies(react-native-simulator react-native-simulator-app-icon)
    endif()
  endif()
endif()

include(GNUInstallDirs)
install(TARGETS react-native-simulator
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT react-native-simulator
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT react-native-simulator
    NAMELINK_COMPONENT react-native-simulator
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT react-native-simulator)
if(RNS_ENABLE_IMGUI)
  install(TARGETS react-native-simulator-inspector
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT react-native-simulator-imgui)
endif()
