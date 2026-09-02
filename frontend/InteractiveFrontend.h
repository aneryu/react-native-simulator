#pragma once

#include <react-native-simulator/Engine.h>

#include <filesystem>
#include <functional>

namespace ReactNativeSimulator {

EngineResult runInteractiveFrontend(
    Engine& engine,
    const std::filesystem::path& fontDirectory = {},
    std::function<void(const std::function<bool()>&)> prepareRuntime = {});

} // namespace ReactNativeSimulator
