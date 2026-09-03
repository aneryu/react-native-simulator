if(NOT DEFINED RNSIM OR NOT DEFINED BUNDLE OR NOT DEFINED PROVENANCE)
  message(FATAL_ERROR "RNSIM, BUNDLE, and PROVENANCE are required")
endif()

file(READ "${PROVENANCE}" provenance)
file(SHA256 "${BUNDLE}" actual_hash)
string(TOLOWER "${actual_hash}" actual_hash)
string(JSON expected_hash GET "${provenance}" sha256)
if(NOT actual_hash STREQUAL expected_hash)
  message(FATAL_ERROR
    "Business bundle SHA-256 mismatch: ${actual_hash} != ${expected_hash}")
endif()

execute_process(
  COMMAND "${RNSIM}" headless
    --profile android-rn87
    --addon compat-rn73
    --bundle "${BUNDLE}"
    --timeout-ms 15000
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rn07310-business-bundle failed (${result}): ${error}\n${output}")
endif()

string(JSON count LENGTH "${provenance}" pass stderrContains)
math(EXPR last "${count} - 1")
foreach(index RANGE 0 ${last})
  string(JSON needle GET "${provenance}" pass stderrContains ${index})
  string(FIND "${error}" "${needle}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "stderr missing '${needle}':\n${error}")
  endif()
endforeach()

string(JSON metrics_line GET "${provenance}" pass metrics schemaVersion)
string(FIND "${output}" "\"schemaVersion\":${metrics_line}" found)
if(found EQUAL -1)
  message(FATAL_ERROR "metrics schemaVersion ${metrics_line} missing:\n${output}")
endif()
