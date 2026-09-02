if(NOT DEFINED RNSIM OR NOT DEFINED PROJECT_DIR)
  message(FATAL_ERROR "RNSIM and PROJECT_DIR are required")
endif()

execute_process(
  COMMAND "${RNSIM}" doctor --json
  WORKING_DIRECTORY "${PROJECT_DIR}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE report
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rnsim doctor failed (${result}): ${error}")
endif()

string(JSON detected GET "${report}" project detected)
string(JSON status GET "${report}" project status)
string(JSON preflight_passed GET "${report}" project preflightPassed)
string(JSON ready_to_launch GET "${report}" project readyToLaunch)
string(JSON compatible GET "${report}" project reactNative compatible)
string(JSON declared GET "${report}" project reactNative declared)
string(JSON platform GET "${report}" project platform)
string(JSON profile GET "${report}" project profile)
string(JSON app_key GET "${report}" project appKey)
string(JSON first_entry GET "${report}" project entries 0)
string(JSON metro_required GET "${report}" project metro required)
string(JSON metro_running GET "${report}" project metro running)
string(JSON metro_project_verified GET
  "${report}" project metro projectVerified)
string(JSON next_action GET "${report}" project nextAction)

if(NOT detected OR NOT compatible OR NOT preflight_passed OR
   NOT declared STREQUAL "0.87.0" OR
   NOT platform STREQUAL "android" OR
   NOT profile STREQUAL "android-rn87" OR
   NOT app_key STREQUAL "DoctorFixture" OR
   NOT first_entry STREQUAL "index.js" OR
   NOT metro_required)
  message(FATAL_ERROR "Unexpected project doctor report: ${report}")
endif()

# Metro project identity is diagnostic only. A reachable Metro is launch-ready
# even when its reported root differs from this fixture or cannot be verified.
if(metro_running)
  if(metro_project_verified)
    if(NOT status STREQUAL "compatible-metro-verified" OR
       NOT ready_to_launch)
      message(FATAL_ERROR "Doctor omitted verified Metro readiness: ${report}")
    endif()
  elseif(NOT ready_to_launch OR
         (NOT status STREQUAL "metro-project-mismatch" AND
          NOT status STREQUAL "metro-project-unverified") OR
         next_action STREQUAL "")
    message(FATAL_ERROR "Doctor blocked on Metro project identity: ${report}")
  endif()
elseif(ready_to_launch OR
       NOT status STREQUAL "compatible-metro-not-running" OR
       NOT next_action MATCHES "Start Metro")
  message(FATAL_ERROR "Doctor omitted the Metro action: ${report}")
endif()
