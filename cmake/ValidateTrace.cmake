if(NOT EXISTS "${TRACE_FILE}")
  message(FATAL_ERROR "Trace file was not created: ${TRACE_FILE}")
endif()
file(READ "${TRACE_FILE}" TRACE_JSON)
foreach(REQUIRED_TEXT
    "\"traceEvents\""
    "\"displayTimeUnit\":\"ms\""
    "\"Runtime initialization\""
    "\"Bundle evaluation\""
    "\"Measured run\"")
  string(FIND "${TRACE_JSON}" "${REQUIRED_TEXT}" FOUND_AT)
  if(FOUND_AT EQUAL -1)
    message(FATAL_ERROR "Trace is missing ${REQUIRED_TEXT}")
  endif()
endforeach()
