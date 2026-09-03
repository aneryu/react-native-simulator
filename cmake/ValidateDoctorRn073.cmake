if(NOT DEFINED RNSIM OR NOT DEFINED PROJECT_DIR OR NOT DEFINED EXPECTED_STATUS)
  message(FATAL_ERROR "RNSIM, PROJECT_DIR, and EXPECTED_STATUS are required")
endif()

set(_addon_args)
if(DEFINED ADDON AND NOT ADDON STREQUAL "")
  list(APPEND _addon_args --addon "${ADDON}")
endif()

execute_process(
  COMMAND "${RNSIM}" doctor --json ${_addon_args}
  WORKING_DIRECTORY "${PROJECT_DIR}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE report
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rnsim doctor failed (${result}): ${error}")
endif()

string(JSON family GET "${report}" project reactNative family)
string(JSON status GET "${report}" project reactNative status)
string(JSON compatible GET "${report}" project reactNative compatible)
string(JSON project_status GET "${report}" project status)
string(JSON declared GET "${report}" project reactNative declared)

if(NOT family STREQUAL "0.73.x" OR
   NOT declared STREQUAL "0.73.10" OR
   NOT status STREQUAL "${EXPECTED_STATUS}")
  message(FATAL_ERROR
    "Unexpected 0.73.x doctor classification: ${report}")
endif()

if(EXPECTED_STATUS STREQUAL "needs-compat-addon")
  if(compatible OR NOT project_status STREQUAL "needs-compat-addon")
    message(FATAL_ERROR "Doctor should require compat-rn73: ${report}")
  endif()
elseif(EXPECTED_STATUS STREQUAL "compatible-via-addon")
  if(NOT compatible)
    message(FATAL_ERROR "Doctor should accept 0.73.x via addon: ${report}")
  endif()
else()
  message(FATAL_ERROR "Unsupported EXPECTED_STATUS ${EXPECTED_STATUS}")
endif()
