if(NOT DEFINED RNSIM OR NOT DEFINED BUNDLE OR NOT DEFINED PROVENANCE)
  message(FATAL_ERROR "RNSIM, BUNDLE, and PROVENANCE are required")
endif()

file(READ "${PROVENANCE}" provenance)
file(SHA256 "${BUNDLE}" actual_hash)
string(TOLOWER "${actual_hash}" actual_hash)
string(JSON expected_hash GET "${provenance}" sha256)
string(TOLOWER "${expected_hash}" expected_hash)
if(NOT actual_hash STREQUAL expected_hash)
  message(FATAL_ERROR
    "Business bundle SHA-256 mismatch: ${actual_hash} != ${expected_hash}")
endif()

set(timeout_ms 15000)
string(JSON timeout_ms ERROR_VARIABLE timeout_err GET "${provenance}" timeoutMs)
if(timeout_err)
  set(timeout_ms 15000)
endif()

execute_process(
  COMMAND "${RNSIM}" headless
    --profile android-rn87
    --addon compat-rn73
    --bundle "${BUNDLE}"
    --timeout-ms "${timeout_ms}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
    "rn07310-business-bundle failed (${result}): ${error}\n${output}")
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

string(JSON metrics_object ERROR_VARIABLE metrics_err GET "${provenance}" pass metrics)
if(metrics_err)
  message(FATAL_ERROR "provenance pass.metrics is required")
endif()

set(_metrics_line "")
string(REPLACE "\n" ";" _output_lines "${output}")
foreach(_line IN LISTS _output_lines)
  if(_line MATCHES "^\\{.*\"host\":\"react-native-simulator\"")
    set(_metrics_line "${_line}")
  endif()
endforeach()
if(_metrics_line STREQUAL "")
  message(FATAL_ERROR "metrics JSON is missing from stdout:\n${output}")
endif()

function(rns_json_pointer_get out_var json dotted_path)
  string(REPLACE "." ";" _parts "${dotted_path}")
  set(_current "${json}")
  foreach(_part IN LISTS _parts)
    string(JSON _current GET "${_current}" "${_part}")
  endforeach()
  set(${out_var} "${_current}" PARENT_SCOPE)
endfunction()

string(JSON _metric_keys LENGTH "${metrics_object}")
# CMake cannot list object keys before 3.19's string(JSON ... MEMBER).
# Walk the documented required pointers plus any extra keys via MEMBER.
string(JSON _member_count LENGTH "${metrics_object}")
math(EXPR _last_member "${_member_count} - 1")
foreach(_index RANGE 0 ${_last_member})
  string(JSON _key MEMBER "${metrics_object}" ${_index})
  string(JSON _expected GET "${metrics_object}" "${_key}")
  rns_json_pointer_get(_actual "${_metrics_line}" "${_key}")
  if(NOT "${_actual}" STREQUAL "${_expected}")
    message(FATAL_ERROR
      "metrics '${_key}' expected '${_expected}' got '${_actual}'\n${_metrics_line}")
  endif()
endforeach()

string(JSON metrics_regex ERROR_VARIABLE regex_err GET "${provenance}" pass metricsRegex)
if(NOT regex_err AND NOT metrics_regex STREQUAL "")
  string(REGEX MATCH "${metrics_regex}" _matched "${_metrics_line}")
  if(NOT _matched)
    message(FATAL_ERROR
      "metrics did not match metricsRegex '${metrics_regex}':\n${_metrics_line}")
  endif()
endif()
