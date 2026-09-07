if(NOT DEFINED RNSIM)
  message(FATAL_ERROR "RNSIM is required")
endif()
if(NOT DEFINED ARGS)
  message(FATAL_ERROR "ARGS is required")
endif()

set(needles)
foreach(var EXPECT1 EXPECT2 EXPECT3 EXPECT4 EXPECT5 EXPECT6)
  if(DEFINED ${var} AND NOT "${${var}}" STREQUAL "")
    list(APPEND needles "${${var}}")
  endif()
endforeach()
if(NOT needles)
  message(FATAL_ERROR "at least one EXPECT* substring is required")
endif()

execute_process(
  COMMAND "${RNSIM}" ${ARGS}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
set(combined "${output}${error}")
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rnsim failed (${result}): ${error}\n${output}")
endif()
foreach(needle IN LISTS needles)
  string(FIND "${combined}" "${needle}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "output missing '${needle}':\n${combined}")
  endif()
endforeach()
