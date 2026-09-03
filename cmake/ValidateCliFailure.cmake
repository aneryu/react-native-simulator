if(NOT DEFINED RNSIM OR NOT DEFINED EXPECTED)
  message(FATAL_ERROR "RNSIM and EXPECTED are required")
endif()

if(NOT DEFINED ARGS)
  set(ARGS)
  if(DEFINED BUNDLE)
    list(APPEND ARGS --bundle "${BUNDLE}")
  endif()
  if(DEFINED PLATFORM)
    list(APPEND ARGS --platform "${PLATFORM}")
  endif()
  if(DEFINED PROFILE)
    list(APPEND ARGS --profile "${PROFILE}")
  endif()
endif()

execute_process(
  COMMAND "${RNSIM}" headless ${ARGS}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(result EQUAL 0)
  message(FATAL_ERROR "rnsim unexpectedly accepted conflicting options")
endif()
if(NOT error MATCHES "${EXPECTED}")
  message(FATAL_ERROR "Unexpected rnsim error: ${error}${output}")
endif()
