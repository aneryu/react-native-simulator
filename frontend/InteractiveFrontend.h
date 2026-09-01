#pragma once

#include <react-native-simulator/Engine.h>

#include <filesystem>

namespace ReactNativeSimulator {

EngineResult runInteractiveFrontend(
    Engine& engine,
    const std::filesystem::path& fontDirectory = {});

} // namespace ReactNativeSimulator
