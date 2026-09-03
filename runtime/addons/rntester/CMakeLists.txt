rns_declare_addon(
  NAME rntester
  SOURCES RNTesterAddon.cpp
  FACTORY createRNTesterAddon
  MODULE
  INSTALL_COMPONENT rntester-demo)
set_target_properties(react-native-simulator-addon-rntester PROPERTIES
  INSTALL_RPATH "")
