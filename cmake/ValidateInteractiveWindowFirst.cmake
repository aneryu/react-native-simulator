if(NOT DEFINED RNSIM OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "RNSIM and OUTPUT are required")
endif()

file(REMOVE "${OUTPUT}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "RNS_INTERACTIVE_SMOKE_OUTPUT=${OUTPUT}"
    RNS_INTERACTIVE_SMOKE_TIMEOUT_MS=500
    "${RNSIM}" interactive
      --url "http://127.0.0.1:1/index.bundle?platform=android&dev=true&minify=false"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
  TIMEOUT 8)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
    "window-first interactive smoke failed (${result}): ${error}${output}")
endif()
if(NOT EXISTS "${OUTPUT}")
  message(FATAL_ERROR "interactive frontend never reported a window")
endif()
file(READ "${OUTPUT}" smoke)
string(JSON window GET "${smoke}" window)
string(JSON ready GET "${smoke}" ready)
if(NOT window OR ready)
  message(FATAL_ERROR "unexpected window-first smoke result: ${smoke}")
endif()
if(NOT error MATCHES "waiting for Metro" OR
   NOT error MATCHES "Metro wait cancelled")
  message(FATAL_ERROR "Metro wait was not visible/cancellable: ${error}")
endif()
