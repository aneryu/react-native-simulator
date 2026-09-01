if(NOT DEFINED BUILD_DIR OR NOT DEFINED INSTALL_PREFIX OR
   NOT DEFINED CONSUMER_BUILD)
  message(FATAL_ERROR
    "BUILD_DIR, INSTALL_PREFIX, and CONSUMER_BUILD are required")
endif()

# These paths are fixed children of the active CMake binary directory. Clean
# them before every run so stale addons or consumer caches cannot make an
# incomplete install appear healthy.
cmake_path(IS_PREFIX BUILD_DIR "${INSTALL_PREFIX}" NORMALIZE
  install_is_under_build)
cmake_path(IS_PREFIX BUILD_DIR "${CONSUMER_BUILD}" NORMALIZE
  consumer_is_under_build)
if(NOT install_is_under_build OR NOT consumer_is_under_build)
  message(FATAL_ERROR "Refusing to clean install-test paths outside BUILD_DIR")
endif()
file(REMOVE_RECURSE "${INSTALL_PREFIX}" "${CONSUMER_BUILD}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
    --prefix "${INSTALL_PREFIX}"
    --component react-native-simulator
  RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Install-tree preparation failed: ${install_result}")
endif()
