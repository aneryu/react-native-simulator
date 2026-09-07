if(NOT DEFINED RNSIM OR NOT DEFINED BUNDLE OR NOT DEFINED ADDON OR NOT DEFINED EXPECTED)
  message(FATAL_ERROR "RNSIM, BUNDLE, ADDON, and EXPECTED are required")
endif()

execute_process(
  COMMAND "${RNSIM}" headless
    --bundle "${BUNDLE}"
    --addon "${ADDON}"
    --timeout-ms 1000
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(result EQUAL 0)
  message(FATAL_ERROR "rnsim unexpectedly accepted ${ADDON}")
endif()
if(NOT error MATCHES "${EXPECTED}" AND NOT output MATCHES "${EXPECTED}")
  message(FATAL_ERROR "Unexpected rnsim error: ${error}${output}")
endif()
