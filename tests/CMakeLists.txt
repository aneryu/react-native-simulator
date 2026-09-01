find_package(Boost CONFIG REQUIRED)
find_package(Threads REQUIRED)

set(RNS_TEST_FIXTURES_DIR "${CMAKE_CURRENT_LIST_DIR}/fixtures")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

add_executable(runtime-api-smoke runtime-api-smoke.cpp)
target_link_libraries(runtime-api-smoke PRIVATE react-native-simulator-engine)

add_executable(headless-api-modules-smoke headless-api-modules-smoke.cpp)
target_link_libraries(
  headless-api-modules-smoke PRIVATE react-native-simulator-engine)

add_executable(devtools-smoke devtools-smoke.cpp)
target_link_libraries(devtools-smoke PRIVATE
  react-native-simulator-engine Boost::boost Threads::Threads)

add_executable(http-bundle-loader-smoke
  http-bundle-loader-smoke.cpp
  "${PROJECT_SOURCE_DIR}/runtime/src/core/HttpBundleLoader.cpp")
target_include_directories(http-bundle-loader-smoke PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/src/core")
target_link_libraries(http-bundle-loader-smoke PRIVATE
  Boost::boost Threads::Threads)

add_executable(reload-engine-smoke reload-engine-smoke.cpp)
target_link_libraries(reload-engine-smoke PRIVATE
  react-native-simulator-engine Threads::Threads)

add_executable(hmr-protocol-smoke hmr-protocol-smoke.cpp)
target_link_libraries(hmr-protocol-smoke PRIVATE
  react-native-simulator-engine Boost::boost Threads::Threads)

foreach(test_target
    runtime-api-smoke
    headless-api-modules-smoke
    devtools-smoke
    http-bundle-loader-smoke
    reload-engine-smoke
    hmr-protocol-smoke)
  rns_enable_sanitizers(${test_target})
endforeach()

if(RNS_ENABLE_SKIA)
  add_executable(skia-scene-smoke skia-scene-smoke.cpp)
  find_path(RNS_FOLLY_INCLUDE_DIR folly/dynamic.h REQUIRED)
  target_include_directories(skia-scene-smoke PRIVATE
    "${RNS_FOLLY_INCLUDE_DIR}")
  target_link_libraries(
    skia-scene-smoke PRIVATE react-native-simulator-skia-renderer)
  rns_enable_sanitizers(skia-scene-smoke)
  add_test(NAME skia-typed-scene COMMAND skia-scene-smoke)

  add_executable(text-example-raster-diag text-example-raster-diag.cpp)
  target_include_directories(text-example-raster-diag PRIVATE
    "${PROJECT_SOURCE_DIR}/render")
  target_link_libraries(text-example-raster-diag PRIVATE
    react-native-simulator-engine
    react-native-simulator-skia-renderer)
  rns_enable_sanitizers(text-example-raster-diag)
endif()

add_test(NAME runtime-embedding-api COMMAND runtime-api-smoke)
add_test(NAME headless-api-modules COMMAND headless-api-modules-smoke)
add_test(NAME metro-http-source COMMAND http-bundle-loader-smoke)
add_test(NAME interactive-reload
  COMMAND reload-engine-smoke "${RNS_TEST_FIXTURES_DIR}/reload-smoke.js")
add_test(NAME hmr-protocol COMMAND hmr-protocol-smoke)
set_tests_properties(hmr-protocol PROPERTIES
  PASS_REGULAR_EXPRESSION
    "HMR_EVAL function.*HMR_SETUP android index.bundle")
add_test(NAME cli-help COMMAND $<TARGET_FILE:react-native-simulator> --help)
set_tests_properties(cli-help PROPERTIES
  PASS_REGULAR_EXPRESSION "rnsim interactive.*rnsim headless.*rnsim doctor")
add_test(NAME cli-version
  COMMAND "${CMAKE_COMMAND}"
    -DRNSIM=$<TARGET_FILE:react-native-simulator>
    -DMODE=version
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateCliMetadata.cmake")
add_test(NAME cli-doctor
  COMMAND "${CMAKE_COMMAND}"
    -DRNSIM=$<TARGET_FILE:react-native-simulator>
    -DMODE=doctor
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateCliMetadata.cmake")
add_test(NAME cli-conformance-unavailable
  COMMAND $<TARGET_FILE:react-native-simulator> test
    --bundle "${RNS_TEST_FIXTURES_DIR}/runtime-smoke.js")
set_tests_properties(cli-conformance-unavailable PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli-no-legacy-command
  COMMAND $<TARGET_FILE:react-native-simulator> run --bundle ignored.js)
set_tests_properties(cli-no-legacy-command PROPERTIES WILL_FAIL TRUE)

set(RNS_RUNTIME_FIXTURE "${RNS_TEST_FIXTURES_DIR}/runtime-smoke.js")
set(RNS_RUNTIME_FIXTURE_HBC "${CMAKE_BINARY_DIR}/tests/runtime-smoke.hbc")
add_test(NAME compile-runtime-fixture
  COMMAND $<TARGET_FILE:hermesc> -emit-binary -out
    "${RNS_RUNTIME_FIXTURE_HBC}" "${RNS_RUNTIME_FIXTURE}")
add_test(NAME runtime-source
  COMMAND $<TARGET_FILE:react-native-simulator> headless
    --bundle "${RNS_RUNTIME_FIXTURE}"
    --iterations 5 --timeout-ms 1000)
add_test(NAME local-config
  COMMAND $<TARGET_FILE:react-native-simulator> headless
    --config "${RNS_TEST_FIXTURES_DIR}/rnsim.json"
    --iterations 5 --timeout-ms 1000)
set_tests_properties(local-config PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"viewport\":\{\"width\":320,\"height\":640,\"pointScaleFactor\":2\}")
add_test(NAME runtime-bytecode
  COMMAND $<TARGET_FILE:react-native-simulator> headless
    --bundle "${RNS_RUNTIME_FIXTURE_HBC}"
    --iterations 5 --timeout-ms 1000)
set_tests_properties(runtime-bytecode PROPERTIES DEPENDS compile-runtime-fixture)
add_test(NAME runtime-multi-bundle
  COMMAND $<TARGET_FILE:react-native-simulator> headless
    --bundle "${RNS_RUNTIME_FIXTURE}"
    --bundle "${RNS_TEST_FIXTURES_DIR}/multi-bundle-secondary.js"
    --iterations 5 --timeout-ms 1000)
add_test(NAME devtools-standalone
  COMMAND $<TARGET_FILE:devtools-smoke>
    "${RNS_TEST_FIXTURES_DIR}/development-session.js"
    "${RNS_REACT_NATIVE_REPOSITORY}/packages/debugger-frontend/dist/third-party/front_end")
add_test(NAME runtime-trace
  COMMAND $<TARGET_FILE:react-native-simulator> headless
    --bundle "${RNS_RUNTIME_FIXTURE}"
    --iterations 5 --timeout-ms 1000
    --trace "${CMAKE_BINARY_DIR}/tests/runtime-trace.json")
add_test(NAME validate-runtime-trace
  COMMAND "${CMAKE_COMMAND}"
    -DTRACE_FILE=${CMAKE_BINARY_DIR}/tests/runtime-trace.json
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateTrace.cmake")
set_tests_properties(validate-runtime-trace PROPERTIES DEPENDS runtime-trace)

set(RNS_INSTALL_TEST_PREFIX "${CMAKE_BINARY_DIR}/tests/install-prefix-v3")
add_test(NAME package-install
  COMMAND "${CMAKE_COMMAND}"
    -DBUILD_DIR=${CMAKE_BINARY_DIR}
    -DINSTALL_PREFIX=${RNS_INSTALL_TEST_PREFIX}
    -DCONSUMER_BUILD=${CMAKE_BINARY_DIR}/tests/install-consumer-build-v3
    -P "${PROJECT_SOURCE_DIR}/cmake/PrepareInstallTest.cmake")
add_test(NAME package-consumer-configure
  COMMAND "${CMAKE_COMMAND}"
    -S "${CMAKE_CURRENT_LIST_DIR}/install-consumer"
    -B "${CMAKE_BINARY_DIR}/tests/install-consumer-build-v3"
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_PREFIX_PATH=${RNS_INSTALL_TEST_PREFIX})
set_tests_properties(package-consumer-configure PROPERTIES DEPENDS package-install)
add_test(NAME package-consumer-build
  COMMAND "${CMAKE_COMMAND}" --build
    "${CMAKE_BINARY_DIR}/tests/install-consumer-build-v3")
set_tests_properties(package-consumer-build PROPERTIES
  DEPENDS package-consumer-configure)
add_test(NAME package-consumer-run
  COMMAND "${CMAKE_BINARY_DIR}/tests/install-consumer-build-v3/rns-consumer")
set_tests_properties(package-consumer-run PROPERTIES DEPENDS package-consumer-build)

add_test(NAME rn73-profile
  COMMAND $<TARGET_FILE:react-native-simulator> headless --bundle
    "${RNS_RUNTIME_FIXTURE}"
    --profile android-rn73 --timeout-ms 1000)
add_test(NAME rn87-profile
  COMMAND $<TARGET_FILE:react-native-simulator> headless --bundle
    "${RNS_RUNTIME_FIXTURE}"
    --profile android-rn87 --timeout-ms 1000)
if(TARGET react-native-simulator-addon-shopee)
  add_test(NAME shopee-addon
    COMMAND $<TARGET_FILE:react-native-simulator> headless --bundle
      "${RNS_TEST_FIXTURES_DIR}/rn73-addon-probe.js"
      --profile android-rn73
      --addon $<TARGET_FILE:react-native-simulator-addon-shopee>
      --timeout-ms 1000)
endif()
add_test(NAME rntester-addon
  COMMAND $<TARGET_FILE:react-native-simulator> headless --bundle
    "${RNS_TEST_FIXTURES_DIR}/rntester-addon-probe.js"
    --profile android-rn87
    --addon $<TARGET_FILE:react-native-simulator-addon-rntester>
    --timeout-ms 1000)

set_tests_properties(runtime-source runtime-bytecode PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"validationMode\":\"workload\".*\"bundleLoaded\":true")
set_tests_properties(runtime-multi-bundle PROPERTIES
  PASS_REGULAR_EXPRESSION "\"bundlesLoaded\":2")
set_tests_properties(rn73-profile PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"profile\":\"android-rn73\".*\"addons\":\\[\\].*\"bundleLoaded\":true")
set_tests_properties(rn87-profile PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"profile\":\"android-rn87\".*\"NativeReactNativeFeatureFlagsCxx\",\"NativeMicrotasksCxx\",\"NativeDOMCxx\",\"NativeAnimatedModule\".*\"IntentAndroid\",\"PermissionsAndroid\".*\"Image\":\"skia-local-and-http-image\".*\"fabric-layout-animation\".*\"bundleLoaded\":true")
if(TEST shopee-addon)
  set_tests_properties(shopee-addon PROPERTIES
    PASS_REGULAR_EXPRESSION
      "\"addons\":\\[\"shopee\"\\].*\"bundleLoaded\":true")
endif()
set_tests_properties(rntester-addon PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"addons\":\\[\"rntester\"\\].*\"NativeCxxModuleExampleCxx\":\"tester-stub\".*\"RNTReportFullyDrawnView\":\"descriptor-only-mock\".*\"bundleLoaded\":true")
set_tests_properties(runtime-trace PROPERTIES
  PASS_REGULAR_EXPRESSION "\"traceFile\":\"")

if(NOT RNS_RNTESTER_BUNDLE)
  set(_rns_default_rntester
    "${CMAKE_BINARY_DIR}/rntester/RNTesterApp.android.jsbundle")
  if(EXISTS "${_rns_default_rntester}")
    set(RNS_RNTESTER_BUNDLE "${_rns_default_rntester}")
  endif()
endif()
if(RNS_REQUIRE_RNTESTER_BUNDLE AND
   (NOT RNS_RNTESTER_BUNDLE OR NOT EXISTS "${RNS_RNTESTER_BUNDLE}"))
  message(FATAL_ERROR
    "RNS_REQUIRE_RNTESTER_BUNDLE=ON requires a caller-built RN Tester bundle")
endif()
if(RNS_RNTESTER_BUNDLE AND EXISTS "${RNS_RNTESTER_BUNDLE}")
  add_test(NAME rntester-android-startup
    COMMAND $<TARGET_FILE:react-native-simulator> headless
      --bundle "${RNS_RNTESTER_BUNDLE}"
      --profile android-rn87
      --app-key RNTesterApp
      --addon $<TARGET_FILE:react-native-simulator-addon-rntester>
      --bundle "${RNS_TEST_FIXTURES_DIR}/rntester-startup-adapter.js"
      --viewport-width 392.7273 --viewport-height 753.4545
      --point-scale-factor 2.75
      --timeout-ms 15000
      --require-react-fabric true)
  set_tests_properties(rntester-android-startup PROPERTIES
    PASS_REGULAR_EXPRESSION
      "\"addons\":\\[\"rntester\"\\].*\"reactFabric\":true.*\"mountedViewTree\":\\{.*\"nodeCount\":[1-9].*\"workloadComplete\":true"
    TIMEOUT 20)
endif()

if(RNS_RN0732_FIXTURE_BUNDLE AND EXISTS "${RNS_RN0732_FIXTURE_BUNDLE}")
  add_test(NAME rn0732-retained-scene-mutations
    COMMAND $<TARGET_FILE:react-native-simulator> headless --bundle
      "${RNS_RN0732_FIXTURE_BUNDLE}"
      --profile android-rn73
      --bundle "${RNS_TEST_FIXTURES_DIR}/rn0732-mutation-workload.js"
      --viewport-width 300 --viewport-height 80 --point-scale-factor 2
      --iterations 2 --timeout-ms 5000
      --require-react-fabric true
      --fail-on-component-fallback true)
  set_tests_properties(rn0732-retained-scene-mutations PROPERTIES
    PASS_REGULAR_EXPRESSION
      "\"reactFabricTransactions\":3.*\"reactFabricCreates\":5.*\"reactFabricInserts\":6.*\"reactFabricUpdates\":5.*\"reactFabricRemoves\":3.*\"reactFabricDeletes\":2.*\"mountedViewTree\":\\{.*\"nativeId\":\"mutation-root\".*\"nativeId\":\"mutation-b\".*\"nativeId\":\"mutation-d\".*\"errors\":\\[\\]"
    TIMEOUT 10)
  add_test(NAME rn0732-textinput-interaction
    COMMAND $<TARGET_FILE:react-native-simulator> headless --bundle
      "${RNS_RN0732_FIXTURE_BUNDLE}"
      --profile android-rn73
      --bundle "${RNS_TEST_FIXTURES_DIR}/rn0732-textinput-workload.js"
      --viewport-width 300 --viewport-height 80
      --iterations 2 --timeout-ms 5000
      --require-react-fabric true
      --fail-on-component-fallback true)
  set_tests_properties(rn0732-textinput-interaction PROPERTIES
    PASS_REGULAR_EXPRESSION
      "\"AndroidTextInput\":\"rn-fabric-headless-platform-adapter\".*\"nativeId\":\"interaction-text-input\".*\"text\":\"A.*\"workloadChecksum\":3"
    TIMEOUT 10)
endif()

set_tests_properties(runtime-source runtime-bytecode runtime-multi-bundle
  runtime-trace validate-runtime-trace rn73-profile rn87-profile
  runtime-embedding-api local-config cli-no-legacy-command
  cli-version cli-doctor cli-conformance-unavailable
  package-install package-consumer-configure
  package-consumer-build package-consumer-run metro-http-source cli-help
  interactive-reload
  PROPERTIES TIMEOUT 10)
set_tests_properties(hmr-protocol PROPERTIES TIMEOUT 20)
if(TEST shopee-addon)
  set_tests_properties(shopee-addon PROPERTIES TIMEOUT 10)
endif()

add_custom_target(verify
  COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
  DEPENDS react-native-simulator
    react-native-simulator-addon-rntester runtime-api-smoke hermesc
    reload-engine-smoke hmr-protocol-smoke
  USES_TERMINAL)
if(TARGET react-native-simulator-addon-shopee)
  add_dependencies(verify react-native-simulator-addon-shopee)
endif()
