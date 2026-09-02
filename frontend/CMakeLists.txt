# Same-process SDL/ImGui host chrome.
#
# Defines the Dear ImGui + SDL3 backend library and the offline replay
# Inspector executable. Added by runtime/CMakeLists.txt when RNS_ENABLE_IMGUI
# is set. InteractiveFrontend.cpp and MacWindowChrome.mm are compiled directly
# into the rnsim executable; that wiring stays in runtime/CMakeLists.txt.
# Visual language: docs/design/CHROME_STYLE.md (ImGui is host shell only;
# Skia paints the RN device).

add_library(react-native-simulator-imgui STATIC
  "${RNS_IMGUI_DIR}/imgui.cpp"
  "${RNS_IMGUI_DIR}/imgui_draw.cpp"
  "${RNS_IMGUI_DIR}/imgui_tables.cpp"
  "${RNS_IMGUI_DIR}/imgui_widgets.cpp"
  "${RNS_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
  "${RNS_IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp"
  "${RNS_IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp")
target_include_directories(react-native-simulator-imgui
  PUBLIC
    "${RNS_IMGUI_DIR}"
    "${RNS_IMGUI_DIR}/backends")
target_link_libraries(react-native-simulator-imgui PUBLIC SDL3::SDL3)
target_compile_options(react-native-simulator-imgui PRIVATE
  -Wall -Wextra -Wpedantic)
set_target_properties(react-native-simulator-imgui PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
rns_enable_sanitizers(react-native-simulator-imgui)

find_program(XXD_EXECUTABLE xxd REQUIRED)
set(RNS_APP_ICON_PNG "${PROJECT_ROOT}/docs/assets/icon-512.png")
set(RNS_APP_ICON_HEADER "${CMAKE_CURRENT_BINARY_DIR}/AppIconPng.h")
if(NOT EXISTS "${RNS_APP_ICON_PNG}")
  message(FATAL_ERROR "Missing app icon PNG: ${RNS_APP_ICON_PNG}")
endif()
add_custom_command(
  OUTPUT "${RNS_APP_ICON_HEADER}"
  COMMAND "${XXD_EXECUTABLE}" -i -n kRnsimAppIconPng
          "${RNS_APP_ICON_PNG}" "${RNS_APP_ICON_HEADER}"
  DEPENDS "${RNS_APP_ICON_PNG}"
  VERBATIM
  COMMENT "Embedding app icon PNG")
add_custom_target(react-native-simulator-app-icon
  DEPENDS "${RNS_APP_ICON_HEADER}")

add_executable(react-native-simulator-inspector
  "${CMAKE_CURRENT_LIST_DIR}/Inspector.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/MacWindowChrome.mm")
set_source_files_properties(
  "${CMAKE_CURRENT_LIST_DIR}/MacWindowChrome.mm" PROPERTIES
  COMPILE_FLAGS "-fobjc-arc")
target_include_directories(react-native-simulator-inspector PRIVATE
  "${FOLLY_INCLUDE_DIR}"
  "${CMAKE_CURRENT_LIST_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(react-native-simulator-inspector PRIVATE
  react-native-simulator-imgui
  rns_folly
  "-framework AppKit")
add_dependencies(react-native-simulator-inspector
  react-native-simulator-app-icon)
add_custom_command(TARGET react-native-simulator-inspector POST_BUILD
  COMMAND osascript -l JavaScript
          "${PROJECT_ROOT}/tools/macos/set-file-icon.js"
          "${PROJECT_ROOT}/docs/assets/AppIcon.icns"
          "$<TARGET_FILE:react-native-simulator-inspector>"
  COMMENT "Setting Inspector file icon"
  VERBATIM)
target_compile_options(react-native-simulator-inspector PRIVATE
  -Wall -Wextra -Wpedantic)
rns_enable_sanitizers(react-native-simulator-inspector)

if(RNS_ENABLE_SKIA)
  target_compile_definitions(react-native-simulator-inspector PRIVATE
    RNS_ENABLE_SKIA=1)
  target_link_libraries(react-native-simulator-inspector PRIVATE
    react-native-simulator-skia-renderer)
endif()
set_target_properties(react-native-simulator-inspector PROPERTIES
  BUILD_RPATH "@loader_path"
  INSTALL_RPATH "@loader_path/../lib")
