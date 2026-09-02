if(NOT DEFINED RNSIM OR NOT DEFINED EXPECTED)
  message(FATAL_ERROR "RNSIM and EXPECTED are required")
endif()

execute_process(
  COMMAND "${RNSIM}" headless
    --bundle "${BUNDLE}"
    --platform ios
    --profile android-rn87
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(result EQUAL 0)
  message(FATAL_ERROR "rnsim unexpectedly accepted conflicting options")
endif()
if(NOT error MATCHES "${EXPECTED}")
  message(FATAL_ERROR "Unexpected rnsim error: ${error}${output}")
endif()
