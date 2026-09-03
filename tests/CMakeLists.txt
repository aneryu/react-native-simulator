find_package(Boost REQUIRED)
find_package(Threads REQUIRED)
if(RNS_ENABLE_IMGUI)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
endif()

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

add_executable(reload-error-recovery-smoke reload-error-recovery-smoke.cpp)
target_link_libraries(reload-error-recovery-smoke PRIVATE
  react-native-simulator-engine Threads::Threads)

add_executable(hmr-protocol-smoke hmr-protocol-smoke.cpp)
target_link_libraries(hmr-protocol-smoke PRIVATE
  react-native-simulator-engine Boost::boost Threads::Threads)

add_subdirectory(addons/fabric-probe)

add_executable(addon-planner-smoke addon-planner-smoke.cpp)
target_link_libraries(addon-planner-smoke PRIVATE react-native-simulator-engine)

add_executable(addon-abi-lifecycle-smoke addon-abi-lifecycle-smoke.cpp)
target_link_libraries(addon-abi-lifecycle-smoke PRIVATE
  react-native-simulator-engine ${CMAKE_DL_LIBS})

add_executable(fabric-module-host fabric-module-host.cpp)
target_link_libraries(fabric-module-host PRIVATE react-native-simulator-engine)
rns_enable_addon_module_host(addon-planner-smoke)
rns_enable_addon_module_host(addon-abi-lifecycle-smoke)
rns_enable_addon_module_host(fabric-module-host)
add_dependencies(addon-planner-smoke react-native-simulator-addon-fabric_probe)
add_dependencies(addon-abi-lifecycle-smoke react-native-simulator-addon-fabric_probe)
add_dependencies(fabric-module-host react-native-simulator-addon-fabric_probe)

add_library(not-an-abi4-addon MODULE not-an-abi4-addon.cpp)
set_target_properties(not-an-abi4-addon PROPERTIES
  PREFIX ""
  OUTPUT_NAME "not-an-abi4-addon")

add_library(invalid-addon-descriptor MODULE invalid-addon-descriptor.cpp)
set_target_properties(invalid-addon-descriptor PROPERTIES
  PREFIX ""
  OUTPUT_NAME "invalid-addon-descriptor")

add_library(wrong-fingerprint-addon MODULE wrong-fingerprint-addon.cpp)
set_target_properties(wrong-fingerprint-addon PROPERTIES
  PREFIX ""
  OUTPUT_NAME "wrong-fingerprint-addon")

foreach(test_target
    runtime-api-smoke
    headless-api-modules-smoke
    devtools-smoke
    http-bundle-loader-smoke
    reload-engine-smoke
    reload-error-recovery-smoke
    hmr-protocol-smoke
    addon-planner-smoke
    addon-abi-lifecycle-smoke
    fabric-module-host)
  rns_enable_sanitizers(${test_target})
endforeach()

if(RNS_ENABLE_SKIA)
  add_executable(skia-scene-smoke skia-scene-smoke.cpp)
  target_include_directories(skia-scene-smoke PRIVATE
    "${FOLLY_INCLUDE_DIR}")
  target_link_libraries(
    skia-scene-smoke PRIVATE
    react-native-simulator-skia-renderer
    rns_folly)
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

if(RNS_ENABLE_SKIA AND RNS_ENABLE_IMGUI)
  add_test(NAME interactive-window-before-metro
    COMMAND "${CMAKE_COMMAND}"
      -DRNSIM=$<TARGET_FILE:react-native-simulator>
      -DOUTPUT=${CMAKE_BINARY_DIR}/tests/window-first-smoke.json
      -P "${PROJECT_SOURCE_DIR}/cmake/ValidateInteractiveWindowFirst.cmake")
  add_test(NAME interactive-preparation-cancel
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/interactive-preparation-cancel.py"
      --rnsim $<TARGET_FILE:react-native-simulator>
      --output
        "${CMAKE_BINARY_DIR}/tests/interactive-preparation-cancel.json")
  set_tests_properties(interactive-preparation-cancel PROPERTIES TIMEOUT 10)
  add_test(NAME cli-doctor-metro-project
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_LIST_DIR}/metro-project-identity.py"
      --rnsim $<TARGET_FILE:react-native-simulator>
      --project-root "${RNS_TEST_FIXTURES_DIR}/doctor-project"
      --other-root "${RNS_TEST_FIXTURES_DIR}")
  set_tests_properties(cli-doctor-metro-project PROPERTIES TIMEOUT 10)
endif()

add_test(NAME runtime-embedding-api COMMAND runtime-api-smoke)
add_test(NAME addon-planner
  COMMAND addon-planner-smoke
    $<TARGET_FILE:react-native-simulator-addon-fabric_probe>)
add_test(NAME addon-abi-lifecycle
  COMMAND addon-abi-lifecycle-smoke
    $<TARGET_FILE:react-native-simulator-addon-fabric_probe>)
add_test(NAME fabric-module-so
  COMMAND fabric-module-host
    $<TARGET_FILE:react-native-simulator-addon-fabric_probe>
    "${RNS_TEST_FIXTURES_DIR}/fabric-probe-workload.js")
add_test(NAME addon-rejects-abi3
  COMMAND "${CMAKE_COMMAND}"
    -DRNSIM=$<TARGET_FILE:react-native-simulator>
    -DBUNDLE=${RNS_TEST_FIXTURES_DIR}/runtime-smoke.js
    -DADDON=$<TARGET_FILE:not-an-abi4-addon>
    "-DEXPECTED=is not an ABI 4 addon"
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateCliAddonFailure.cmake")
add_test(NAME addon-rejects-short-descriptor
  COMMAND "${CMAKE_COMMAND}"
    -DRNSIM=$<TARGET_FILE:react-native-simulator>
    -DBUNDLE=${RNS_TEST_FIXTURES_DIR}/runtime-smoke.js
    -DADDON=$<TARGET_FILE:invalid-addon-descriptor>
    "-DEXPECTED=Invalid addon descriptor"
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateCliAddonFailure.cmake")
add_test(NAME addon-rejects-fingerprint
  COMMAND "${CMAKE_COMMAND}"
    -DRNSIM=$<TARGET_FILE:react-native-simulator>
    -DBUNDLE=${RNS_TEST_FIXTURES_DIR}/runtime-smoke.js
    -DADDON=$<TARGET_FILE:wrong-fingerprint-addon>
    "-DEXPECTED=Addon API fingerprint mismatch"
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateCliAddonFailure.cmake")
add_test(NAME headless-api-modules COMMAND headless-api-modules-smoke)
add_test(NAME metro-http-source COMMAND http-bundle-loader-smoke)
add_test(NAME interactive-reload
  COMMAND reload-engine-smoke "${RNS_TEST_FIXTURES_DIR}/reload-smoke.js")
add_test(NAME interactive-error-recovery COMMAND reload-error-recovery-smoke)
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
add_test(NAME cli-doctor-project
  COMMAND "${CMAKE_COMMAND}"
    -DRNSIM=$<TARGET_FILE:react-native-simulator>
    -DPROJECT_DIR=${RNS_TEST_FIXTURES_DIR}/doctor-project
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateDoctorProject.cmake")
add_test(NAME cli-doctor-expo-project
  COMMAND "${CMAKE_COMMAND}"
    -DRNSIM=$<TARGET_FILE:react-native-simulator>
    -DPROJECT_DIR=${RNS_TEST_FIXTURES_DIR}/doctor-expo-project
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateDoctorExpoProject.cmake")
add_test(NAME cli-conformance-unavailable
  COMMAND $<TARGET_FILE:react-native-simulator> test
    --bundle "${RNS_TEST_FIXTURES_DIR}/runtime-smoke.js")
set_tests_properties(cli-conformance-unavailable PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli-no-legacy-command
  COMMAND $<TARGET_FILE:react-native-simulator> run --bundle ignored.js)
set_tests_properties(cli-no-legacy-command PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli-profile-platform-conflict
  COMMAND "${CMAKE_COMMAND}"
    -DRNSIM=$<TARGET_FILE:react-native-simulator>
    -DBUNDLE=${RNS_TEST_FIXTURES_DIR}/runtime-smoke.js
    -DPLATFORM=ios
    -DPROFILE=android-rn87
    "-DEXPECTED=--platform ios is incompatible with --profile android-rn87"
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateCliFailure.cmake")

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

add_test(NAME rn73-profile
  COMMAND "${CMAKE_COMMAND}"
    -DRNSIM=$<TARGET_FILE:react-native-simulator>
    -DBUNDLE=${RNS_RUNTIME_FIXTURE}
    -DPROFILE=android-rn73
    "-DEXPECTED=profile android-rn73 was removed"
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateCliFailure.cmake")
add_test(NAME rn87-profile
  COMMAND $<TARGET_FILE:react-native-simulator> headless --bundle
    "${RNS_RUNTIME_FIXTURE}"
    --profile android-rn87 --timeout-ms 1000)
if(TARGET react-native-simulator-addon-shopee)
  add_test(NAME shopee-addon
    COMMAND $<TARGET_FILE:react-native-simulator> headless --bundle
      "${RNS_TEST_FIXTURES_DIR}/rn73-addon-probe.js"
      --profile android-rn87
      --addon $<TARGET_FILE:react-native-simulator-addon-shopee>
      --timeout-ms 1000)
endif()
if(TARGET react-native-simulator-addon-rntester)
  add_custom_target(rntester-addon-for-tests ALL
    DEPENDS react-native-simulator-addon-rntester)
endif()
add_test(NAME rntester-addon
  COMMAND $<TARGET_FILE:react-native-simulator> headless --bundle
    "${RNS_TEST_FIXTURES_DIR}/rntester-addon-probe.js"
    --profile android-rn87
    --addon $<TARGET_FILE:react-native-simulator-addon-rntester>
    --timeout-ms 1000)
add_test(NAME expo-addon
  COMMAND $<TARGET_FILE:react-native-simulator> headless --bundle
    "${RNS_TEST_FIXTURES_DIR}/expo-addon-probe.js"
    --profile android-rn87
    --addon expo
    --timeout-ms 1000)
add_test(NAME cli-list-addons
  COMMAND $<TARGET_FILE:react-native-simulator> --list-addons --json)
set_tests_properties(cli-list-addons PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"name\":\"expo\".*\"name\":\"safe-area\".*\"name\":\"compat-rn73\"")
add_test(NAME cli-no-addon-safe-area
  COMMAND $<TARGET_FILE:react-native-simulator> headless
    --bundle "${RNS_TEST_FIXTURES_DIR}/safe-area-disabled.js"
    --profile android-rn87
    --no-addon safe-area
    --timeout-ms 1000)
set_tests_properties(cli-no-addon-safe-area PROPERTIES
  PASS_REGULAR_EXPRESSION "\"workloadChecksum\":8")
add_test(NAME cli-no-addon-safe-area-fallback
  COMMAND $<TARGET_FILE:react-native-simulator> headless
    --bundle "${RNS_TEST_FIXTURES_DIR}/safe-area-fallback.js"
    --profile android-rn87
    --no-addon safe-area
    --fail-on-component-fallback true
    --timeout-ms 1000)
set_tests_properties(cli-no-addon-safe-area-fallback PROPERTIES WILL_FAIL TRUE)
add_test(NAME compat-rn73-constants
  COMMAND $<TARGET_FILE:react-native-simulator> headless
    --bundle "${RNS_TEST_FIXTURES_DIR}/compat-rn73-constants.js"
    --profile android-rn87
    --addon compat-rn73
    --timeout-ms 1000)
set_tests_properties(compat-rn73-constants PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"targetFamily\":\"0.73.x\".*\"jsVisibleReactNativeVersion\":\"0.73.10\".*\"workloadChecksum\":73")
add_test(NAME local-config-schema1
  COMMAND "${CMAKE_COMMAND}"
    -DRNSIM=$<TARGET_FILE:react-native-simulator>
    -DCONFIG=${RNS_TEST_FIXTURES_DIR}/rnsim-schema1.json
    "-DEXPECTED=schemaVersion 1 is no longer accepted"
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateCliConfigFailure.cmake")
add_test(NAME local-config-bare-addon
  COMMAND "${CMAKE_COMMAND}"
    -DRNSIM=$<TARGET_FILE:react-native-simulator>
    -DCONFIG=${RNS_TEST_FIXTURES_DIR}/rnsim-bare-addon.json
    "-DEXPECTED=addons entries must be \\{name\\} or \\{path\\} objects"
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateCliConfigFailure.cmake")
add_test(NAME local-config-tagged-compat
  COMMAND $<TARGET_FILE:react-native-simulator> headless
    --config "${RNS_TEST_FIXTURES_DIR}/rnsim-tagged-compat.json"
    --timeout-ms 1000)
set_tests_properties(local-config-tagged-compat PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"compatAddon\":\"compat-rn73\".*\"targetFamily\":\"0.73.x\"")
add_test(NAME cli-doctor-rn073
  COMMAND "${CMAKE_COMMAND}"
    -DRNSIM=$<TARGET_FILE:react-native-simulator>
    -DPROJECT_DIR=${RNS_TEST_FIXTURES_DIR}/doctor-rn073-project
    -DEXPECTED_STATUS=needs-compat-addon
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateDoctorRn073.cmake")
add_test(NAME cli-doctor-rn073-compat
  COMMAND "${CMAKE_COMMAND}"
    -DRNSIM=$<TARGET_FILE:react-native-simulator>
    -DPROJECT_DIR=${RNS_TEST_FIXTURES_DIR}/doctor-rn073-project
    -DADDON=compat-rn73
    -DEXPECTED_STATUS=compatible-via-addon
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidateDoctorRn073.cmake")
set_tests_properties(expo-addon PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"name\":\"expo\".*\"name\":\"ExpoAsset\".*\"bundleLoaded\":true")

set_tests_properties(runtime-source runtime-bytecode PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"validationMode\":\"workload\".*\"bundleLoaded\":true")
set_tests_properties(runtime-multi-bundle PROPERTIES
  PASS_REGULAR_EXPRESSION "\"bundlesLoaded\":2")
if(RNS_ENABLE_SKIA)
  set(RNS_TEST_IMAGE_FIDELITY "skia-local-and-http-image")
else()
  set(RNS_TEST_IMAGE_FIDELITY "unavailable-without-skia")
endif()
set_tests_properties(rn87-profile PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"name\":\"android-rn87\".*\"Image\".*\"NativeMicrotasksCxx\".*\"IntentAndroid\".*\"bundleLoaded\":true")
if(TEST shopee-addon)
  set_tests_properties(shopee-addon PROPERTIES
    PASS_REGULAR_EXPRESSION
      "\"name\":\"shopee\".*\"bundleLoaded\":true")
endif()
set_tests_properties(rntester-addon PROPERTIES
  PASS_REGULAR_EXPRESSION
    "\"name\":\"rntester\".*\"RNTReportFullyDrawnView\".*\"NativeCxxModuleExampleCxx\".*\"bundleLoaded\":true")
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
  if(RNS_ENABLE_SKIA AND RNS_ENABLE_IMGUI)
    add_test(NAME interactive-preparation-retry
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/interactive-preparation-retry.py"
        --rnsim $<TARGET_FILE:react-native-simulator>
        --bundle "${RNS_RNTESTER_BUNDLE}"
        --addon $<TARGET_FILE:react-native-simulator-addon-rntester>
        --output
          "${CMAKE_BINARY_DIR}/tests/interactive-preparation-retry.json")
    set_tests_properties(interactive-preparation-retry PROPERTIES TIMEOUT 45)
  endif()
endif()

if(RNS_RN0732_FIXTURE_BUNDLE AND EXISTS "${RNS_RN0732_FIXTURE_BUNDLE}")
  add_test(NAME rn0732-retained-scene-mutations
    COMMAND $<TARGET_FILE:react-native-simulator> headless --bundle
      "${RNS_RN0732_FIXTURE_BUNDLE}"
      --profile android-rn87 --addon compat-rn73
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
      --profile android-rn87 --addon compat-rn73
      --bundle "${RNS_TEST_FIXTURES_DIR}/rn0732-textinput-workload.js"
      --viewport-width 300 --viewport-height 80
      --iterations 2 --timeout-ms 5000
      --require-react-fabric true
      --fail-on-component-fallback true)
  set_tests_properties(rn0732-textinput-interaction PROPERTIES
    PASS_REGULAR_EXPRESSION
      "\"name\":\"AndroidTextInput\".*\"nativeId\":\"interaction-text-input\".*\"text\":\"A.*\"workloadChecksum\":3"
    TIMEOUT 10)
endif()

if(RNS_REQUIRE_RN073_BUSINESS_BUNDLE)
  if(NOT RNS_RN073_BUSINESS_BUNDLE OR
     NOT EXISTS "${RNS_RN073_BUSINESS_BUNDLE}" OR
     NOT RNS_RN073_BUSINESS_PROVENANCE OR
     NOT EXISTS "${RNS_RN073_BUSINESS_PROVENANCE}")
    message(FATAL_ERROR
      "RNS_REQUIRE_RN073_BUSINESS_BUNDLE=ON requires RNS_RN073_BUSINESS_BUNDLE and RNS_RN073_BUSINESS_PROVENANCE")
  endif()
  file(READ "${RNS_RN073_BUSINESS_PROVENANCE}" _rns_provenance)
  file(SHA256 "${RNS_RN073_BUSINESS_BUNDLE}" _rns_bundle_hash)
  string(TOLOWER "${_rns_bundle_hash}" _rns_bundle_hash)
  string(JSON _rns_expected_hash GET "${_rns_provenance}" sha256)
  string(TOLOWER "${_rns_expected_hash}" _rns_expected_hash)
  if(NOT _rns_bundle_hash STREQUAL _rns_expected_hash)
    message(FATAL_ERROR
      "RNS_RN073_BUSINESS_BUNDLE SHA-256 ${_rns_bundle_hash} does not match provenance ${_rns_expected_hash}")
  endif()
  string(JSON _rns_schema GET "${_rns_provenance}" schemaVersion)
  if(NOT _rns_schema EQUAL 1)
    message(FATAL_ERROR "RNS_RN073_BUSINESS_PROVENANCE schemaVersion must be 1")
  endif()
endif()
if(RNS_RN073_BUSINESS_BUNDLE AND EXISTS "${RNS_RN073_BUSINESS_BUNDLE}" AND
   RNS_RN073_BUSINESS_PROVENANCE AND EXISTS "${RNS_RN073_BUSINESS_PROVENANCE}")
  add_test(NAME rn07310-business-bundle
    COMMAND "${CMAKE_COMMAND}"
      -DRNSIM=$<TARGET_FILE:react-native-simulator>
      -DBUNDLE=${RNS_RN073_BUSINESS_BUNDLE}
      -DPROVENANCE=${RNS_RN073_BUSINESS_PROVENANCE}
      -P "${PROJECT_SOURCE_DIR}/cmake/ValidateRn073BusinessBundle.cmake")
  set_tests_properties(rn07310-business-bundle PROPERTIES TIMEOUT 30)
endif()

set_tests_properties(runtime-source runtime-bytecode runtime-multi-bundle
  runtime-trace validate-runtime-trace rn73-profile rn87-profile
  runtime-embedding-api local-config cli-no-legacy-command
  cli-version cli-doctor cli-doctor-project cli-doctor-expo-project
  cli-conformance-unavailable
  cli-profile-platform-conflict
  metro-http-source cli-help
  interactive-reload interactive-error-recovery
  addon-planner addon-abi-lifecycle fabric-module-so addon-rejects-abi3
  addon-rejects-short-descriptor addon-rejects-fingerprint
  cli-list-addons cli-no-addon-safe-area cli-no-addon-safe-area-fallback
  compat-rn73-constants local-config-schema1 local-config-bare-addon
  local-config-tagged-compat cli-doctor-rn073 cli-doctor-rn073-compat
  PROPERTIES TIMEOUT 10)
if(TEST interactive-window-before-metro)
  set_tests_properties(interactive-window-before-metro PROPERTIES TIMEOUT 10)
endif()
set_tests_properties(hmr-protocol PROPERTIES TIMEOUT 20)
if(TEST shopee-addon)
  set_tests_properties(shopee-addon PROPERTIES TIMEOUT 10)
endif()
set_tests_properties(expo-addon PROPERTIES TIMEOUT 10)

add_custom_target(verify
  COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
  DEPENDS react-native-simulator
    react-native-simulator-addon-rntester runtime-api-smoke hermesc
    reload-engine-smoke reload-error-recovery-smoke hmr-protocol-smoke
    addon-planner-smoke addon-abi-lifecycle-smoke fabric-module-host
    react-native-simulator-addon-fabric_probe
    not-an-abi4-addon invalid-addon-descriptor wrong-fingerprint-addon
  USES_TERMINAL)
if(TARGET react-native-simulator-addon-shopee)
  add_dependencies(verify react-native-simulator-addon-shopee)
endif()
