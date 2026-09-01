# React Native 0.87 host overrides

These files are simulator-owned overrides for the pinned React Native 0.87 CXX
platform surface. They keep `third_party/react-native` clean while providing
the host paragraph props, vertical alignment, inline attachment measurement,
and `measureLines` contract required by the simulator.

`runtime/CMakeLists.txt` places this tree before ReactCommon includes and
replaces the matching upstream CXX sources explicitly. Re-audit every override
when changing the pinned React Native revision; do not patch the submodule.
