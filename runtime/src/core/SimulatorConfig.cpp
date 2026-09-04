#include "SimulatorConfig.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <folly/json.h>

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string_view>

namespace {
std::filesystem::path resolvePath(
    const std::filesystem::path& configPath,
    const std::string& value) {
  std::filesystem::path path(value);
  if (path.is_relative()) {
    path = configPath.parent_path() / path;
  }
  return std::filesystem::weakly_canonical(path);
}

void validateObjectKeys(
    const boost::property_tree::ptree& object,
    std::initializer_list<std::string_view> allowed,
    std::string_view location) {
  std::set<std::string> seen;
  for (const auto& [key, value] : object) {
    (void)value;
    if (!seen.insert(key).second) {
      throw std::invalid_argument(
          "Duplicate rnsim.json field: " + std::string(location) + key);
    }
    if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
      throw std::invalid_argument(
          "Unknown rnsim.json field: " + std::string(location) + key);
    }
  }
}
} // namespace

std::string normalizeInitialPropsJson(const std::string& json) {
  try {
    auto parsed = json.empty() ? folly::dynamic::object() : folly::parseJson(json);
    if (!parsed.isObject()) {
      throw std::invalid_argument("initialProps must be a JSON object");
    }
    return folly::toJson(parsed);
  } catch (const std::invalid_argument&) {
    throw;
  } catch (const std::exception& error) {
    throw std::invalid_argument(
        std::string("initialProps JSON: ") + error.what());
  }
}

SimulatorLocalConfig loadSimulatorConfig(const std::filesystem::path& path) {
  boost::property_tree::ptree json;
  try {
    boost::property_tree::read_json(path.string(), json);
  } catch (const boost::property_tree::json_parser::json_parser_error& error) {
    throw std::invalid_argument(
        "Cannot parse config " + path.string() + ": " + error.message());
  }
  validateObjectKeys(
      json,
      {"schemaVersion", "reactNative", "platform", "appKey", "initialProps",
       "bundle", "viewport", "fonts", "environment", "addons",
       "disabledAddons", "autoAddons"},
      "");
  if (const auto viewport = json.get_child_optional("viewport")) {
    validateObjectKeys(
        *viewport, {"width", "height", "pointScaleFactor"}, "viewport.");
  }
  if (const auto fonts = json.get_child_optional("fonts")) {
    validateObjectKeys(*fonts, {"directory"}, "fonts.");
  }

  SimulatorLocalConfig config;
  config.schemaVersion = json.get<int>("schemaVersion", 0);
  if (config.schemaVersion == 1) {
    throw std::invalid_argument(
        "rnsim.json schemaVersion 1 is no longer accepted; use schemaVersion 2 with tagged addons entries");
  }
  if (config.schemaVersion != 2) {
    throw std::invalid_argument("rnsim.json schemaVersion must be 2");
  }
  config.reactNative = json.get<std::string>("reactNative", "");
  if (config.reactNative.empty()) {
    throw std::invalid_argument("rnsim.json requires reactNative");
  }
  if (config.reactNative != RNS_REACT_NATIVE_VERSION) {
    throw std::invalid_argument(
        "This binary supports reactNative " RNS_REACT_NATIVE_VERSION ", not " +
        config.reactNative);
  }
  config.platform = json.get<std::string>("platform", "android");
  if (config.platform != "android" && config.platform != "ios") {
    throw std::invalid_argument(
        "rnsim.json platform must be android or ios");
  }
  if (const auto appKey = json.get_optional<std::string>("appKey")) {
    if (appKey->empty()) {
      throw std::invalid_argument("rnsim.json appKey must not be empty");
    }
    config.appKey = *appKey;
  }
  if (json.get_child_optional("initialProps")) {
    std::ifstream input(path);
    if (!input) {
      throw std::invalid_argument(
          "Cannot re-read config " + path.string() + " for initialProps");
    }
    const std::string body(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    try {
      const auto parsed = folly::parseJson(body);
      const auto* props = parsed.get_ptr("initialProps");
      if (props == nullptr || !props->isObject()) {
        throw std::invalid_argument(
            "rnsim.json initialProps must be a JSON object");
      }
      config.initialPropsJson = folly::toJson(*props);
    } catch (const std::invalid_argument&) {
      throw;
    } catch (const std::exception& error) {
      throw std::invalid_argument(
          std::string("rnsim.json initialProps JSON: ") + error.what());
    }
  }
  if (const auto bundle = json.get_optional<std::string>("bundle")) {
    config.bundle = resolvePath(path, *bundle);
  }
  if (const auto value = json.get_optional<float>("viewport.width")) {
    config.viewportWidth = *value;
  }
  if (const auto value = json.get_optional<float>("viewport.height")) {
    config.viewportHeight = *value;
  }
  if (const auto value =
          json.get_optional<float>("viewport.pointScaleFactor")) {
    config.pointScaleFactor = *value;
  }
  if (const auto value = json.get_optional<std::string>("fonts.directory")) {
    config.fontDirectory = resolvePath(path, *value);
  }
  if (const auto addons = json.get_child_optional("addons")) {
    for (const auto& [key, value] : *addons) {
      if (!key.empty()) {
        throw std::invalid_argument("rnsim.json addons must be an array");
      }
      if (value.empty() && value.data().empty() == false) {
        throw std::invalid_argument(
            "rnsim.json addons entries must be {name} or {path} objects");
      }
      const auto name = value.get_optional<std::string>("name");
      const auto addonPath = value.get_optional<std::string>("path");
      if (static_cast<bool>(name) == static_cast<bool>(addonPath)) {
        throw std::invalid_argument(
            "rnsim.json addons entries must contain exactly one of name or path");
      }
      validateObjectKeys(
          value,
          name ? std::initializer_list<std::string_view>{"name"}
               : std::initializer_list<std::string_view>{"path"},
          "addons[].");
      SimulatorAddonConfigEntry entry;
      if (name) {
        entry.name = *name;
      } else {
        entry.path = resolvePath(path, *addonPath);
      }
      config.addons.push_back(std::move(entry));
    }
  }
  if (const auto disabled = json.get_child_optional("disabledAddons")) {
    for (const auto& [key, value] : *disabled) {
      if (!key.empty()) {
        throw std::invalid_argument("rnsim.json disabledAddons must be an array");
      }
      config.disabledAddons.push_back(value.get_value<std::string>());
    }
  }
  if (const auto autoAddons = json.get_optional<bool>("autoAddons")) {
    config.autoAddons = *autoAddons;
  }
  if (const auto environment = json.get_child_optional("environment")) {
    validateObjectKeys(
        *environment,
        {"colorScheme",
         "appState",
         "reduceMotion",
         "invertColors",
         "highTextContrast",
         "screenReader",
         "accessibilityService",
         "grayscale",
         "boldText",
         "reduceTransparency",
         "darkerSystemColors",
         "orientation"},
        "environment.");
    auto stringField = [&](const char* key, std::optional<std::string>& out) {
      if (const auto value = environment->get_optional<std::string>(key)) {
        out = *value;
      }
    };
    auto boolField = [&](const char* key, std::optional<bool>& out) {
      if (const auto value = environment->get_optional<bool>(key)) {
        out = *value;
      }
    };
    stringField("colorScheme", config.environment.colorScheme);
    stringField("appState", config.environment.appState);
    stringField("orientation", config.environment.orientation);
    boolField("reduceMotion", config.environment.reduceMotion);
    boolField("invertColors", config.environment.invertColors);
    boolField("highTextContrast", config.environment.highTextContrast);
    boolField("screenReader", config.environment.screenReader);
    boolField("accessibilityService", config.environment.accessibilityService);
    boolField("grayscale", config.environment.grayscale);
    boolField("boldText", config.environment.boldText);
    boolField("reduceTransparency", config.environment.reduceTransparency);
    boolField("darkerSystemColors", config.environment.darkerSystemColors);
    if (config.environment.colorScheme &&
        *config.environment.colorScheme != "light" &&
        *config.environment.colorScheme != "dark") {
      throw std::invalid_argument(
          "rnsim.json environment.colorScheme must be light or dark");
    }
    if (config.environment.appState &&
        *config.environment.appState != "active" &&
        *config.environment.appState != "background" &&
        *config.environment.appState != "inactive") {
      throw std::invalid_argument(
          "rnsim.json environment.appState must be active, background, or inactive");
    }
    if (config.environment.orientation) {
      const auto& name = *config.environment.orientation;
      if (name != "portrait-primary" && name != "portrait-secondary" &&
          name != "landscape-primary" && name != "landscape-secondary") {
        throw std::invalid_argument(
            "rnsim.json environment.orientation must be portrait-primary, "
            "portrait-secondary, landscape-primary, or landscape-secondary");
      }
    }
  }
  return config;
}
