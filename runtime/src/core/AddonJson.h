#pragma once

#include <react-native-simulator/SimulatorAddon.h>

#include <stdexcept>
#include <string>
#include <string_view>

namespace ReactNativeSimulator {

inline const char* jsonCapabilityClass(RuntimeCapabilityClass value) {
  switch (value) {
    case RuntimeCapabilityClass::Implemented:
      return "implemented";
    case RuntimeCapabilityClass::HostAdapted:
      return "host-adapted";
    case RuntimeCapabilityClass::Mocked:
      return "mocked";
    case RuntimeCapabilityClass::LayoutOnly:
      return "layout-only";
    case RuntimeCapabilityClass::Unavailable:
      return "unavailable";
  }
  throw std::logic_error("unknown RuntimeCapabilityClass");
}

inline const char* jsonComponentKind(AddonComponentKind value) {
  switch (value) {
    case AddonComponentKind::DescriptorOnlyMock:
      return "descriptor-only-mock";
    case AddonComponentKind::FabricDescriptor:
      return "fabric-descriptor";
  }
  throw std::logic_error("unknown AddonComponentKind");
}

inline const char* jsonAddonSource(AddonSource value) {
  switch (value) {
    case AddonSource::BuiltIn:
      return "built-in";
    case AddonSource::Module:
      return "module";
    case AddonSource::InProcess:
      return "in-process";
  }
  throw std::logic_error("unknown AddonSource");
}

inline const char* jsonAddonOrigin(AddonRequestOrigin value) {
  switch (value) {
    case AddonRequestOrigin::Auto:
      return "auto";
    case AddonRequestOrigin::Config:
      return "config";
    case AddonRequestOrigin::Cli:
      return "cli";
    case AddonRequestOrigin::Embedder:
      return "embedder";
    case AddonRequestOrigin::Test:
      return "test";
  }
  throw std::logic_error("unknown AddonRequestOrigin");
}

inline const char* jsonAddonAutoPolicy(AddonAutoPolicy value) {
  switch (value) {
    case AddonAutoPolicy::Always:
      return "always";
    case AddonAutoPolicy::Expo:
      return "expo";
    case AddonAutoPolicy::Never:
      return "never";
  }
  throw std::logic_error("unknown AddonAutoPolicy");
}

inline const char* jsonAddonRole(AddonRole value) {
  switch (value) {
    case AddonRole::Application:
      return "application";
    case AddonRole::Community:
      return "community";
    case AddonRole::VersionCompat:
      return "version-compat";
  }
  throw std::logic_error("unknown AddonRole");
}

inline const char* jsonSimulatorMode(SimulatorMode value) {
  switch (value) {
    case SimulatorMode::Headless:
      return "headless";
    case SimulatorMode::Interactive:
      return "interactive";
    case SimulatorMode::Conformance:
      return "conformance";
  }
  throw std::logic_error("unknown SimulatorMode");
}

inline bool validAddonName(std::string_view name) {
  if (name.empty() || name.front() < 'a' || name.front() > 'z') {
    return false;
  }
  for (char c : name) {
    if ((c < 'a' || c > 'z') && (c < '0' || c > '9') && c != '-') {
      return false;
    }
  }
  return true;
}

inline bool looksLikeAddonModulePath(std::string_view token) {
  if (token.find('/') != std::string_view::npos) {
    return true;
  }
  if (token.starts_with(".") || token.starts_with("..")) {
    return true;
  }
  return token.ends_with(".so") || token.ends_with(".dylib");
}

} // namespace ReactNativeSimulator
