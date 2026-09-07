#pragma once

#include <react-native-simulator/SimulatorAddon.h>

#include <folly/dynamic.h>
#include <memory>
#include <string>
#include <vector>

namespace ReactNativeSimulator {

struct BuiltinAddonCatalogEntry {
  const char* key;
  AddonAutoPolicy autoPolicy;
  std::unique_ptr<SimulatorAddon> (*create)();
};

const std::vector<BuiltinAddonCatalogEntry>& builtinAddonCatalog();

folly::dynamic builtinAddonCatalogJson();

inline const BuiltinAddonCatalogEntry* findBuiltinAddon(std::string_view key) {
  for (const auto& entry : builtinAddonCatalog()) {
    if (entry.key == key) {
      return &entry;
    }
  }
  return nullptr;
}

} // namespace ReactNativeSimulator
