if(NOT DEFINED RNSIM OR NOT DEFINED MODE)
  message(FATAL_ERROR "RNSIM and MODE are required")
endif()

if(MODE STREQUAL "version")
  execute_process(
    COMMAND "${RNSIM}" --version --json
    RESULT_VARIABLE result
    OUTPUT_VARIABLE metadata
    ERROR_VARIABLE error)
elseif(MODE STREQUAL "doctor")
  execute_process(
    COMMAND "${RNSIM}" doctor --json
    RESULT_VARIABLE result
    OUTPUT_VARIABLE metadata
    ERROR_VARIABLE error)
else()
  message(FATAL_ERROR "Unsupported validation mode: ${MODE}")
endif()

if(NOT result EQUAL 0)
  message(FATAL_ERROR "rnsim ${MODE} failed (${result}): ${error}")
endif()

string(JSON version GET "${metadata}" version)
string(JSON channel GET "${metadata}" channel)
string(JSON transport_version GET "${metadata}" transportVersion)
string(JSON react_native GET "${metadata}" reactNative)
string(JSON addon_abi GET "${metadata}" addonAbi)
if(NOT version STREQUAL "nightly" OR
   NOT channel STREQUAL "nightly" OR
   NOT transport_version STREQUAL "0.0.0" OR
   NOT react_native STREQUAL "0.87.0" OR
   NOT addon_abi EQUAL 2)
  message(FATAL_ERROR "Unexpected ${MODE} metadata: ${metadata}")
endif()

if(MODE STREQUAL "doctor")
  string(JSON status GET "${metadata}" status)
  string(JSON sandbox GET "${metadata}" securitySandbox)
  string(JSON conformance GET "${metadata}" features conformance)
  if(NOT status STREQUAL "experimental-android-first" OR sandbox OR conformance)
    message(FATAL_ERROR "Doctor overclaims release capability: ${metadata}")
  endif()
endif()
