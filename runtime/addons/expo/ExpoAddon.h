#pragma once

#include <memory>

#include <react-native-simulator/SimulatorAddon.h>

std::unique_ptr<ReactNativeSimulator::SimulatorAddon> createExpoAddon();
