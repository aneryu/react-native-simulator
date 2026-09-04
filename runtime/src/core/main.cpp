#include <react-native-simulator/Engine.h>
#include <react-native-simulator/SimulatorAddon.h>

#include "AddonJson.h"
#include "BuiltinAddonCatalog.h"
#include "HttpBundleLoader.h"
#include "SimulatorConfig.h"

#if RNS_ENABLE_SKIA
#include "SkiaPngExporter.h"
#endif
#if RNS_ENABLE_IMGUI
#include "InteractiveFrontend.h"
#endif

#include "HostPlatform.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <folly/json.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <sys/utsname.h>

#ifndef RNS_ENABLE_SKIA
#define RNS_ENABLE_SKIA 0
#endif
#ifndef RNS_ENABLE_IMGUI
#define RNS_ENABLE_IMGUI 0
#endif

namespace rns = ReactNativeSimulator;

namespace {
bool parseBoolean(const std::string &name, const std::string &value) {
  if (value == "true" || value == "1") {
    return true;
  }
  if (value == "false" || value == "0") {
    return false;
  }
  throw std::invalid_argument(name + " must be true or false");
}

struct CliOptions {
  enum class Mode {
    Interactive,
    Headless,
    Conformance,
  };

  Mode mode{Mode::Interactive};
  rns::EngineConfig runtime;
  struct BundleSource {
    std::string source;
    bool http{false};
    bool defaultMetro{false};
  };
  std::vector<BundleSource> bundles;
  struct AddonToken {
    std::string token;
    rns::AddonRequestOrigin origin{rns::AddonRequestOrigin::Cli};
  };
  std::vector<AddonToken> addons;
  std::vector<std::string> disabledAddons;
  bool autoAddons{true};
  std::optional<std::string> initialUrl;
  std::optional<std::filesystem::path> outputPath;
  std::optional<std::filesystem::path> screenshotPath;
};

bool isScriptFile(const std::filesystem::path &path) {
  const auto ext = path.extension().string();
  return ext == ".js" || ext == ".jsx" || ext == ".ts" || ext == ".tsx";
}

std::string toBundlePath(std::string sourceFile) {
  const auto slash = sourceFile.rfind('/');
  const auto dot = sourceFile.rfind('.');
  if (dot != std::string::npos &&
      (slash == std::string::npos || dot > slash)) {
    sourceFile.resize(dot);
  }
  return sourceFile + ".bundle";
}

std::optional<std::string> readJsonString(
    const std::filesystem::path &path,
    const char *field) {
  if (!std::filesystem::exists(path)) {
    return std::nullopt;
  }
  try {
    boost::property_tree::ptree json;
    boost::property_tree::read_json(path.string(), json);
    if (const auto value = json.get_optional<std::string>(field)) {
      if (!value->empty()) {
        return *value;
      }
    }
  } catch (const std::exception&) {
  }
  return std::nullopt;
}

std::optional<std::string> appJsonName(const std::filesystem::path &root) {
  auto name = readJsonString(root / "app.json", "name");
  if (name && name->find('/') != std::string::npos) {
    return std::nullopt;
  }
  return name;
}

bool jsonHasChild(const std::filesystem::path &path, const char *field) {
  if (!std::filesystem::exists(path)) {
    return false;
  }
  try {
    boost::property_tree::ptree json;
    boost::property_tree::read_json(path.string(), json);
    return json.get_child_optional(field).has_value();
  } catch (const std::exception &) {
  }
  return false;
}

struct ExpoProject {
  bool detected{false};
  std::optional<std::string> declared;
  std::optional<std::string> installed;
  bool router{false};
};

ExpoProject detectExpoProject(const std::filesystem::path &root) {
  ExpoProject expo;
  expo.declared = readJsonString(root / "package.json", "dependencies.expo");
  if (!expo.declared) {
    expo.declared =
        readJsonString(root / "package.json", "devDependencies.expo");
  }
  expo.installed =
      readJsonString(root / "node_modules/expo/package.json", "version");
  expo.router =
      readJsonString(root / "package.json", "dependencies.expo-router")
          .has_value() ||
      readJsonString(root / "package.json", "devDependencies.expo-router")
          .has_value() ||
      std::filesystem::exists(root / "node_modules/expo-router");
  expo.detected = expo.declared.has_value() || expo.installed.has_value() ||
      jsonHasChild(root / "app.json", "expo") ||
      jsonHasChild(root / "app.config.json", "expo");
  return expo;
}

std::optional<std::filesystem::path> resolveProjectScript(
    const std::filesystem::path &root,
    std::string specifier) {
  if (specifier.empty() || specifier[0] == '/' ||
      specifier.find("://") != std::string::npos) {
    return std::nullopt;
  }
  while (specifier.size() >= 2 && specifier[0] == '.' && specifier[1] == '/') {
    specifier.erase(0, 2);
  }
  const auto tryFile =
      [&](const std::filesystem::path &candidate)
      -> std::optional<std::filesystem::path> {
    std::error_code error;
    if (!std::filesystem::is_regular_file(candidate, error) ||
        !isScriptFile(candidate)) {
      return std::nullopt;
    }
    auto relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.generic_string().starts_with("..")) {
      return std::nullopt;
    }
    return relative;
  };
  const auto trySpecifier =
      [&](const std::filesystem::path &base)
      -> std::optional<std::filesystem::path> {
    if (const auto hit = tryFile(base)) {
      return hit;
    }
    for (const char *ext : {".js", ".jsx", ".ts", ".tsx"}) {
      if (const auto hit = tryFile(std::filesystem::path(base.string() + ext))) {
        return hit;
      }
    }
    for (const char *name :
         {"index.js", "index.jsx", "index.ts", "index.tsx"}) {
      if (const auto hit = tryFile(base / name)) {
        return hit;
      }
    }
    return std::nullopt;
  };
  if (const auto hit = trySpecifier(root / specifier)) {
    return hit;
  }
  if (specifier[0] != '.') {
    if (const auto hit = trySpecifier(root / "node_modules" / specifier)) {
      return hit;
    }
  }
  return std::nullopt;
}

void addEntryCandidate(
    std::vector<std::string> &entries,
    std::unordered_set<std::string> &seen,
    const std::filesystem::path &root,
    const std::filesystem::path &relative) {
  const auto full = root / relative;
  std::error_code error;
  if (!std::filesystem::is_regular_file(full, error) || !isScriptFile(full)) {
    return;
  }
  auto generic = relative.generic_string();
  if (seen.insert(generic).second) {
    entries.push_back(std::move(generic));
  }
}

std::vector<std::string> discoverMetroEntries(
    const std::filesystem::path &root,
    const std::string &platform) {
  std::vector<std::string> entries;
  std::unordered_set<std::string> seen;
  if (const auto name = appJsonName(root)) {
    addEntryCandidate(entries, seen, root, *name + ".js");
    addEntryCandidate(
        entries, seen, root, std::filesystem::path("js") / (*name + ".js"));
    addEntryCandidate(
        entries,
        seen,
        root,
        std::filesystem::path("js") / (*name + "." + platform + ".js"));
    addEntryCandidate(
        entries, seen, root, std::filesystem::path("src") / (*name + ".js"));
  }
  if (const auto main = readJsonString(root / "package.json", "main")) {
    if (const auto resolved = resolveProjectScript(root, *main)) {
      addEntryCandidate(entries, seen, root, *resolved);
    }
  }
  if (detectExpoProject(root).detected) {
    addEntryCandidate(
        entries, seen, root, "node_modules/expo/AppEntry.js");
    addEntryCandidate(
        entries, seen, root, "node_modules/expo-router/entry.js");
  }
  addEntryCandidate(
      entries, seen, root, "src/index." + platform + ".js");
  addEntryCandidate(entries, seen, root, "src/index.js");
  for (const char *dirName : {"js", "src"}) {
    const auto dir = root / dirName;
    std::error_code error;
    if (!std::filesystem::is_directory(dir, error)) {
      continue;
    }
    const auto suffix = "." + platform + ".js";
    for (const auto &entry : std::filesystem::directory_iterator(dir, error)) {
      if (!entry.is_regular_file(error) || !isScriptFile(entry.path())) {
        continue;
      }
      const auto name = entry.path().filename().string();
      if (name.size() > suffix.size() &&
          name.compare(name.size() - suffix.size(), suffix.size(), suffix) ==
              0) {
        addEntryCandidate(
            entries, seen, root, std::filesystem::path(dirName) / name);
      }
    }
  }
  return entries;
}

std::optional<std::string> inferAppKeyFromProject(
    const std::filesystem::path &root) {
  // Expo registerRootComponent always uses AppRegistry key "main". expo.name
  // is a display string, not the native application key.
  if (detectExpoProject(root).detected) {
    return std::string("main");
  }
  // Entry filenames describe Metro input, not AppRegistry identity. Prefer the
  // caller's app.json name when present; otherwise let the live AppRegistry
  // select its sole key or ask the developer to choose among multiple keys.
  return appJsonName(root);
}

std::string defaultMetroBundlePath(
    const std::filesystem::path &root,
    const std::string &platform) {
  std::string path = "index.bundle";
  const bool hasRootIndex =
      std::filesystem::exists(root / "index.js") ||
      std::filesystem::exists(root / ("index." + platform + ".js"));
  if (!hasRootIndex) {
    const auto entries = discoverMetroEntries(root, platform);
    if (!entries.empty()) {
      path = toBundlePath(entries.front());
    }
  }
  return path;
}

bool bundleUrlLooksLikeExpo(const std::string &url) {
  return url.find("expo-router/") != std::string::npos ||
      url.find("node_modules/expo/") != std::string::npos ||
      url.find("/expo/AppEntry") != std::string::npos;
}

std::optional<std::filesystem::path> metroProjectRootFromError(
    const std::string &body) {
  try {
    const auto json = folly::parseJson(body);
    const auto path = json.getDefault("originModulePath", "").asString();
    if (path.empty()) {
      return std::nullopt;
    }
    std::filesystem::path root(path);
    if (root.filename() == ".") {
      root = root.parent_path();
    }
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
      return std::nullopt;
    }
    return std::filesystem::weakly_canonical(root);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

constexpr std::string_view kMetroProjectProbeEntry =
    "__rnsim_project_root_probe_8f3d6c4a__";

std::filesystem::path canonicalProjectRoot(
    const std::filesystem::path &path) {
  std::error_code error;
  auto canonical = std::filesystem::weakly_canonical(path, error);
  if (error || !std::filesystem::is_directory(canonical, error) || error) {
    throw std::runtime_error(
        "Cannot resolve project root " + path.string());
  }
  return canonical;
}

bool sameProjectRoot(
    const std::filesystem::path &expected,
    const std::filesystem::path &actual) {
  std::error_code error;
  const bool equivalent = std::filesystem::equivalent(expected, actual, error);
  return !error ? equivalent : expected == actual;
}

std::filesystem::path probeMetroProjectRoot(
    const std::string &bundleUrl,
    const std::function<bool()> &cancelled = {}) {
  const auto query = bundleUrl.find('?', std::string("http://").size());
  const auto probeUrl =
      metroOrigin(bundleUrl) + "/" + std::string(kMetroProjectProbeEntry) +
      ".bundle" +
      (query == std::string::npos ? std::string{} : bundleUrl.substr(query));
  try {
    (void)fetchHttpBundle(probeUrl, 3000, cancelled);
    throw std::runtime_error(
        "Metro project probe unexpectedly resolved " + probeUrl);
  } catch (const MetroHttpError &error) {
    if (error.status != 404 ||
        error.body.find("UnableToResolveError") == std::string::npos) {
      throw std::runtime_error(
          "Metro project probe at " + metroOrigin(bundleUrl) +
          " returned HTTP " + std::to_string(error.status) +
          " without project-root evidence");
    }
    const auto root = metroProjectRootFromError(error.body);
    if (!root) {
      throw std::runtime_error(
          "Metro project probe at " + metroOrigin(bundleUrl) +
          " did not report a usable originModulePath");
    }
    return canonicalProjectRoot(*root);
  }
}

struct MetroProjectCheck {
  bool verified{false};
  std::optional<std::filesystem::path> actualRoot;
  std::string error;
};

MetroProjectCheck checkMetroProject(
    const std::string &bundleUrl,
    const std::filesystem::path &expectedRoot,
    const std::function<bool()> &cancelled = {}) {
  MetroProjectCheck check;
  try {
    check.actualRoot = probeMetroProjectRoot(bundleUrl, cancelled);
    check.verified = sameProjectRoot(expectedRoot, *check.actualRoot);
  } catch (const HttpRequestCancelled &) {
    throw;
  } catch (const std::exception &error) {
    check.error = error.what();
  }
  return check;
}

std::string bundleOrigin(const std::string &bundleUrl) {
  const auto pathStart = bundleUrl.find('/', std::string("http://").size());
  if (pathStart == std::string::npos) {
    throw std::invalid_argument("Metro URL is missing a path");
  }
  return bundleUrl.substr(0, pathStart);
}

bool metroStatusRunning(const std::string &bundleUrl) {
  try {
    const auto body =
        fetchHttpBundle(bundleOrigin(bundleUrl) + "/status", 800);
    return body.find("packager-status:running") != std::string::npos;
  } catch (const std::exception&) {
    return false;
  }
}

#if RNS_ENABLE_IMGUI
bool waitForMetro(
    const std::string &bundleUrl,
    const std::function<bool()> &cancelled) {
  if (cancelled()) {
    return false;
  }
  if (metroStatusRunning(bundleUrl)) {
    return true;
  }
  const auto origin = bundleOrigin(bundleUrl);
  const bool expo = detectExpoProject(std::filesystem::current_path()).detected;
  std::cerr << "waiting for Metro at " << origin << '\n'
            << "start it with " << (expo ? "npx expo start" : "npm start")
            << "; rnsim does not launch Metro\n";
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (!metroStatusRunning(bundleUrl)) {
    if (cancelled()) {
      std::cerr << "Metro wait cancelled\n";
      return false;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      throw std::runtime_error(
          "Timed out waiting for Metro at " + origin +
          ". Start Metro or pass --url/--bundle.");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }
  std::cerr << "connected to Metro\n";
  return true;
}
#endif

bool isIndexBundleUrl(const std::string &url) {
  const auto pathStart = url.find('/', std::string("http://").size());
  if (pathStart == std::string::npos) {
    return false;
  }
  const auto queryStart = url.find('?', pathStart);
  const auto path = queryStart == std::string::npos
      ? url.substr(pathStart)
      : url.substr(pathStart, queryStart - pathStart);
  return path == "/index.bundle";
}

std::string fetchDefaultMetroBundle(
    CliOptions::BundleSource &bundle,
    rns::EngineConfig *runtime,
    const std::function<bool()> &cancelled = {},
    std::optional<std::filesystem::path> *discoveredProject = nullptr) {
  const auto rememberProject =
      [&](const std::optional<std::filesystem::path> &project) {
        if (discoveredProject != nullptr && project) {
          *discoveredProject = project;
        }
        if (runtime != nullptr && !runtime->appKey && project) {
          runtime->appKey = inferAppKeyFromProject(*project);
        }
      };
  try {
    auto bytes = fetchHttpBundle(bundle.source, 60000, cancelled);
    try {
      rememberProject(probeMetroProjectRoot(bundle.source, cancelled));
    } catch (const HttpRequestCancelled &) {
      throw;
    } catch (const std::exception &) {
    }
    return bytes;
  } catch (const MetroHttpError &error) {
    if (!bundle.defaultMetro || !isIndexBundleUrl(bundle.source) ||
        error.status != 404 ||
        error.body.find("UnableToResolveError") == std::string::npos) {
      throw;
    }
    const auto platform =
        bundle.source.find("platform=ios") != std::string::npos ? "ios"
                                                                : "android";
    const auto project = metroProjectRootFromError(error.body);
    if (!project) {
      throw;
    }
    rememberProject(project);
    const auto entries = discoverMetroEntries(*project, platform);
    std::exception_ptr lastError;
    const auto limit = std::min<size_t>(entries.size(), 4);
    for (size_t index = 0; index < limit; ++index) {
      const auto &entry = entries[index];
      auto fallback = bundleOrigin(bundle.source) + "/" + toBundlePath(entry);
      const auto query = bundle.source.find('?');
      if (query != std::string::npos) {
        fallback += bundle.source.substr(query);
      }
      std::cerr << "Metro has no ./index; trying " << fallback << '\n';
      try {
        auto bytes = fetchHttpBundle(fallback, 60000, cancelled);
        bundle.source = std::move(fallback);
        return bytes;
      } catch (const HttpRequestCancelled &) {
        throw;
      } catch (const std::exception&) {
        lastError = std::current_exception();
      }
    }
    if (lastError) {
      std::rethrow_exception(lastError);
    }
    throw;
  }
}

void printHelp() {
  std::cout << R"(rnsim

Usage:
  rnsim [options]               Start an interactive session
  rnsim interactive [options]   Start an interactive session
  rnsim headless [options]      Run a finite headless workload
  rnsim doctor [--json] [--url URL]
                                 Diagnose this installation, RN project, and Metro
  rnsim --version [--json]      Print build and runtime contract versions

Common options:
  --config FILE           Local config (default: ./rnsim.json when present)
  --platform android|ios  Metro target platform (default: android)
  --profile NAME          RN contract: android-rn87, ios-rn87, macos-rn87
  --bundle FILE           Load a caller-built local bundle
  --addon NAME_OR_PATH    Built-in catalog key or MODULE path
  --no-addon NAME         Disable one auto-selected catalog key
  --no-auto-addons        Disable every automatic addon
  --list-addons [--json]  Print the compiled addon catalog
  --initial-url URL       Freeze the session initial URL
  --android-font-dir DIR  Fonts used by Skia measurement and paint

Interactive options:
  --url URL               Complete loopback bundle URL
  --app-key NAME          AppRegistry key (preselected in the interactive launcher)
  --initial-props JSON    JSON object passed as runApplication initialProps
  --devtools              Enable and open the bundled React Native DevTools
  --no-open               Enable DevTools without opening its frontend
  --devtools-frontend-dir DIR  Override the bundled DevTools frontend

With no --bundle/--url, interactive mode opens its window, then waits for Metro
at localhost:8081 and loads index.bundle. Closing the window cancels that wait.
If Metro has no ./index, rnsim reads the packager project path from the error
and tries entry files found there, including Expo AppEntry and expo-router/entry.
app.json name becomes --app-key; Expo projects use AppRegistry key main.
Expo projects also auto-load the built-in Expo boot adapter. `safe-area`
auto-loads for every project. Pass --no-addon safe-area to disable it.
Expo SDK 57 (RN 0.86) interactive sessions warn and continue; they are not a
0.86 native profile. Expo Router, Reanimated, Screens, and Gesture Handler
remain unavailable.

Headless options:
  --iterations N          Workload size
  --timeout-ms N          Workload completion timeout
  --output FILE           Persist metrics JSON
  --trace FILE            Persist Chrome Trace JSON
  --screenshot FILE       Persist the retained Skia scene as PNG
  --devtools              Inspect the workload with React Native DevTools

Interactive mode does not require RN$SimulatorWorkload signals;
headless mode remains strict and finite. Public conformance is intentionally
disabled during Nightly until canonical profile/font/oracle manifests are complete.
)";
}

folly::dynamic buildInformation() {
  struct utsname system{};
  const bool unameOk = ::uname(&system) == 0;
  return folly::dynamic::object
      ("name", "react-native-simulator")
      ("version", RNS_PROJECT_VERSION)
      ("channel", RNS_PROJECT_VERSION)
      ("transportVersion", RNS_TRANSPORT_VERSION)
      ("commit", RNS_BUILD_COMMIT)
      ("dirty", std::string(RNS_BUILD_DIRTY) == "true")
      ("reactNative", RNS_REACT_NATIVE_VERSION)
      ("hermes", RNS_HERMES_VERSION)
      ("addonAbi", rns::kSimulatorAddonAbiVersion)
      ("hostOs", hostOsName())
      ("minimumMacOS", RNS_BUILD_MIN_MACOS)
      ("architecture", unameOk ? system.machine : "unknown")
      ("systemRelease", unameOk ? system.release : "unknown")
      ("features", folly::dynamic::object
          ("interactive", RNS_ENABLE_IMGUI != 0)
          ("skia", RNS_ENABLE_SKIA != 0)
          ("devtoolsBackend", true)
          ("conformance", false));
}

void printVersion(bool json) {
  const auto info = buildInformation();
  if (json) {
    std::cout << folly::toJson(info) << '\n';
    return;
  }
  std::cout << "react-native-simulator " << info["version"].asString()
            << " (commit " << info["commit"].asString()
            << (info["dirty"].asBool() ? ", dirty" : ", clean") << ")\n"
            << "React Native " << info["reactNative"].asString()
            << ", Hermes " << info["hermes"].asString()
            << ", addon ABI " << info["addonAbi"].asInt() << '\n';
  const auto host = info["hostOs"].asString();
  if (host == "macos") {
    std::cout << "macOS " << info["minimumMacOS"].asString()
              << "+ " << info["architecture"].asString();
  } else {
    std::cout << host << " " << info["architecture"].asString();
  }
  std::cout << "; interactive="
            << (info["features"]["interactive"].asBool() ? "yes" : "no")
            << ", Skia="
            << (info["features"]["skia"].asBool() ? "yes" : "no") << '\n';
}

std::string stripVersionPrefix(std::string version) {
  while (!version.empty() &&
         (version.front() == '^' || version.front() == '~' ||
          version.front() == '=' || version.front() == 'v')) {
    version.erase(version.begin());
  }
  return version;
}

bool isReactNative073Family(const std::optional<std::string>& version) {
  if (!version) {
    return false;
  }
  const auto normalized = stripVersionPrefix(*version);
  return normalized.rfind("0.73.", 0) == 0 || normalized == "0.73";
}

void printDoctor(
    const char* argv0,
    bool json,
    const std::optional<std::string>& configuredMetroUrl = std::nullopt,
    bool requestedCompatRn73 = false) {
  auto report = buildInformation();
  std::error_code error;
  auto executable = std::filesystem::weakly_canonical(argv0, error);
  if (error) executable = std::filesystem::absolute(argv0, error);
  const auto installRoot = executable.parent_path().parent_path();
  const auto frontend = installRoot /
      "share/react-native-simulator/debugger-frontend/rn_fusebox.html";
  error.clear();
  const auto projectRoot = canonicalProjectRoot(std::filesystem::current_path());
  const auto packageJson = projectRoot / "package.json";
  const auto appJson = projectRoot / "app.json";
  const auto localConfig = projectRoot / "rnsim.json";
  const bool packagePresent = std::filesystem::is_regular_file(packageJson);
  const auto declaredReactNative = readJsonString(
      packageJson, "dependencies.react-native");
  const auto declaredDevReactNative = declaredReactNative
      ? std::optional<std::string>{}
      : readJsonString(packageJson, "devDependencies.react-native");
  const auto declaredVersion = declaredReactNative
      ? declaredReactNative
      : declaredDevReactNative;
  const auto installedVersion = readJsonString(
      projectRoot / "node_modules/react-native/package.json", "version");
  const auto effectiveVersion = installedVersion
      ? installedVersion
      : declaredVersion;
  const auto exactVersion = [](const std::optional<std::string>& version) {
    return version && *version == RNS_REACT_NATIVE_VERSION;
  };
  const auto familyVersion = installedVersion ? installedVersion : declaredVersion;
  const bool exactCompatible = installedVersion
      ? exactVersion(installedVersion)
      : exactVersion(declaredVersion);
  const bool family073 = isReactNative073Family(familyVersion);
  std::string reactNativeFamily = exactCompatible
      ? "0.87.0"
      : family073 ? "0.73.x" : (familyVersion ? "other" : "");
  std::string reactNativeStatus = exactCompatible
      ? "compatible"
      : family073
          ? (requestedCompatRn73 ? "compatible-via-addon" : "needs-compat-addon")
          : "unsupported";
  const bool reactNativeCompatible =
      exactCompatible || (family073 && requestedCompatRn73);

  std::string platform = "android";
  std::optional<std::string> configuredAppKey;
  std::optional<std::filesystem::path> configuredBundle;
  bool configValid = true;
  std::string configError;
  if (std::filesystem::is_regular_file(localConfig)) {
    try {
      const auto config = loadSimulatorConfig(localConfig);
      platform = config.platform;
      configuredAppKey = config.appKey;
      configuredBundle = config.bundle;
    } catch (const std::exception& configException) {
      configValid = false;
      configError = configException.what();
    }
  }

  std::vector<std::string> entries;
  std::unordered_set<std::string> seenEntries;
  const auto addDoctorEntry = [&](const std::filesystem::path& relative) {
    std::error_code entryError;
    if (!std::filesystem::is_regular_file(
            projectRoot / relative, entryError)) {
      return;
    }
    const auto value = relative.generic_string();
    if (seenEntries.insert(value).second) {
      entries.push_back(value);
    }
  };
  addDoctorEntry("index." + platform + ".js");
  addDoctorEntry("index.js");
  for (const auto& entry : discoverMetroEntries(projectRoot, platform)) {
    if (seenEntries.insert(entry).second) {
      entries.push_back(entry);
    }
  }
  const auto expo = detectExpoProject(projectRoot);
  const auto appKey = configuredAppKey
      ? configuredAppKey
      : inferAppKeyFromProject(projectRoot);
  // Match launch source precedence exactly. An explicit doctor URL diagnoses
  // that Metro source even when rnsim.json names a local bundle. Otherwise a
  // configured bundle remains selected even when its path is missing.
  const bool offlineBundleSelected =
      !configuredMetroUrl && configuredBundle.has_value();
  const bool offlineBundlePresent = offlineBundleSelected &&
      std::filesystem::is_regular_file(*configuredBundle);
  const bool hasEntry = configuredMetroUrl.has_value() ||
      (offlineBundleSelected ? offlineBundlePresent : !entries.empty());
  const std::string metroUrl = configuredMetroUrl.value_or(
      "http://localhost:8081/" +
      defaultMetroBundlePath(projectRoot, platform) + "?platform=" +
      platform + "&dev=true&minify=false");
  const bool metroRequired = !offlineBundleSelected;
  const bool metroRunning = metroRequired && metroStatusRunning(metroUrl);
  MetroProjectCheck metroProject;
  std::string metroProjectVerification = "not-running";
  if (!metroRequired) {
    metroProject.verified = true;
    metroProjectVerification = "not-required-local-bundle";
  } else if (metroRunning) {
    metroProject = checkMetroProject(metroUrl, projectRoot);
    metroProjectVerification = metroProject.verified
        ? "probe-match"
        : metroProject.actualRoot ? "probe-mismatch" : "probe-failed";
  }
  const bool projectDetected = packagePresent && effectiveVersion.has_value();
  const bool launchable = projectDetected && reactNativeCompatible &&
      configValid && hasEntry;
  const bool preflightPassed = launchable;
  const bool readyToLaunch =
      launchable && (!metroRequired || metroRunning);
  std::string projectStatus = offlineBundleSelected
      ? offlineBundlePresent ? "ready-offline" : "missing-configured-bundle"
      : !metroRunning ? "compatible-metro-not-running"
      : metroProject.verified ? "compatible-metro-verified"
      : metroProject.actualRoot ? "metro-project-mismatch"
                                : "metro-project-unverified";
  std::string nextAction;
  if (!projectDetected) {
    projectStatus = "not-react-native-project";
    nextAction = "Run rnsim doctor from an RN 0.87 application root.";
  } else if (!reactNativeCompatible) {
    projectStatus = family073 ? "needs-compat-addon" : "incompatible-react-native";
    nextAction = family073
        ? "Use --profile android-rn87 --addon compat-rn73 for RN 0.73.x JS."
        : expo.detected
        ? std::string("Use React Native ") + RNS_REACT_NATIVE_VERSION +
            " with this rnsim binary. Expo SDK 57 ships RN 0.86; an Expo "
            "project must install react-native@" +
            RNS_REACT_NATIVE_VERSION + " (currently expo@canary)."
        : std::string("Use React Native ") + RNS_REACT_NATIVE_VERSION +
            " with this rnsim binary.";
  } else if (!configValid) {
    projectStatus = "invalid-config";
    nextAction = configError;
  } else if (offlineBundleSelected && !offlineBundlePresent) {
    projectStatus = "missing-configured-bundle";
    nextAction = "Configured bundle is not a regular file: " +
        configuredBundle->string();
  } else if (!hasEntry) {
    projectStatus = "missing-entry";
    nextAction = expo.detected
        ? "Install expo so node_modules/expo/AppEntry.js exists, configure a "
          "bundle in rnsim.json, or pass --url/--bundle."
        : "Add index.js, configure a bundle in rnsim.json, or pass --url/--bundle.";
  } else if (metroRequired && !metroRunning) {
    projectStatus = "compatible-metro-not-running";
    nextAction = expo.detected
        ? "Start Metro with npx expo start."
        : "Start Metro with npm start or yarn start.";
  } else if (metroRequired && !metroProject.verified) {
    if (metroProject.actualRoot) {
      nextAction =
          "Metro serves " + metroProject.actualRoot->string() +
          " while doctor ran from " + projectRoot.string() +
          "; launch will use the selected Metro source.";
    } else {
      nextAction =
          "Metro project root could not be verified; launch will use the "
          "selected Metro source. " + metroProject.error;
    }
  }

  folly::dynamic entryJson = folly::dynamic::array;
  for (const auto& entry : entries) {
    entryJson.push_back(entry);
  }
  const auto optionalString = [](const auto& value) -> folly::dynamic {
    return value ? folly::dynamic(value->string()) : folly::dynamic(nullptr);
  };
  const auto optionalText = [](const auto& value) -> folly::dynamic {
    return value ? folly::dynamic(*value) : folly::dynamic(nullptr);
  };
  report["executable"] = executable.string();
  report["installedDevToolsFrontend"] =
      std::filesystem::is_regular_file(frontend);
  report["localConfig"] = std::filesystem::is_regular_file(localConfig)
      ? folly::dynamic(localConfig.string())
      : folly::dynamic(nullptr);
  report["defaultProfile"] = "android-rn87";
  report["status"] = "experimental-android-first";
  report["securitySandbox"] = false;
  folly::dynamic unsupported = folly::dynamic::array;
  if (expo.router) {
    unsupported.push_back("expo-router");
    unsupported.push_back("react-native-screens");
    unsupported.push_back("react-native-reanimated");
    unsupported.push_back("react-native-gesture-handler");
  }
  report["project"] = folly::dynamic::object
      ("root", projectRoot.string())
      ("detected", projectDetected)
      ("kind", expo.detected ? "expo" : "react-native")
      ("status", projectStatus)
      ("preflightPassed", preflightPassed)
      ("readyToLaunch", readyToLaunch)
      ("nextAction", nextAction)
      ("packageJson", packagePresent)
      ("reactNative", folly::dynamic::object
          ("expected", RNS_REACT_NATIVE_VERSION)
          ("declared", optionalText(declaredVersion))
          ("installed", optionalText(installedVersion))
          ("effective", optionalText(effectiveVersion))
          ("family", reactNativeFamily)
          ("status", reactNativeStatus)
          ("compatible", reactNativeCompatible))
      ("platform", platform)
      ("profile", platform + "-rn87")
      ("appKey", optionalText(appKey))
      ("entries", std::move(entryJson))
      ("expo", folly::dynamic::object
          ("detected", expo.detected)
          ("declared", optionalText(expo.declared))
          ("installed", optionalText(expo.installed))
          ("sdk", optionalText(expo.installed ? expo.installed : expo.declared))
          ("router", expo.router)
          ("appRegistryKey", expo.detected ? folly::dynamic("main")
                                           : folly::dynamic(nullptr))
          ("unsupportedNativeContracts", std::move(unsupported)))
      ("config", folly::dynamic::object
          ("path", std::filesystem::is_regular_file(localConfig)
              ? folly::dynamic(localConfig.string())
              : folly::dynamic(nullptr))
          ("valid", configValid)
          ("error", configError.empty()
              ? folly::dynamic(nullptr)
              : folly::dynamic(configError))
          ("bundle", optionalString(configuredBundle))
          ("bundlePresent", configuredBundle
              ? folly::dynamic(std::filesystem::is_regular_file(
                    *configuredBundle))
              : folly::dynamic(nullptr)))
      ("metro", folly::dynamic::object
          ("url", metroUrl)
          ("required", metroRequired)
          ("running", metroRunning)
          ("expectedProjectRoot", projectRoot.string())
          ("actualProjectRoot", optionalString(metroProject.actualRoot))
          ("projectVerified", metroProject.verified)
          ("projectVerification", metroProjectVerification)
          ("projectVerificationError", metroProject.error.empty()
              ? folly::dynamic(nullptr)
              : folly::dynamic(metroProject.error)));
  if (json) {
    std::cout << folly::toJson(report) << '\n';
    return;
  }
  printVersion(false);
  std::cout << "executable: " << report["executable"].asString() << '\n'
            << "profile: android-rn87 (experimental Android-first)\n"
            << "DevTools frontend: "
            << (report["installedDevToolsFrontend"].asBool()
                    ? "installed"
                    : "external frontend required")
            << '\n'
            << "local config: "
            << (report["localConfig"].isNull()
                    ? "none"
                    : report["localConfig"].asString())
            << '\n'
            << "project: " << projectRoot.string() << '\n'
            << "kind: " << (expo.detected ? "expo" : "react-native") << '\n'
            << "React Native: "
            << (effectiveVersion ? *effectiveVersion : "not detected")
            << " (" << reactNativeStatus << ")\n"
            << (expo.detected
                    ? "Expo: " +
                        (expo.installed ? *expo.installed
                                        : expo.declared ? *expo.declared
                                                        : std::string("detected")) +
                        (expo.router ? " (expo-router not hosted)\n" : "\n")
                    : "")
            << "source: "
            << (offlineBundleSelected
                    ? configuredBundle->string()
                    : configuredMetroUrl
                        ? *configuredMetroUrl
                        : entries.empty() ? "not found" : entries.front())
            << '\n'
            << "AppRegistry key: " << (appKey ? *appKey : "auto-detect")
            << '\n'
            << "Metro: "
            << (!metroRequired ? "not required for configured bundle"
                               : metroRunning
                                   ? "reachable at " + bundleOrigin(metroUrl)
                                   : "not running at " + bundleOrigin(metroUrl))
            << '\n'
            << (metroRequired && metroRunning
                    ? metroProject.verified
                        ? "Metro project: verified " + projectRoot.string() + "\n"
                        : metroProject.actualRoot
                            ? "Metro project: mismatch; actual " +
                                metroProject.actualRoot->string() + "\n"
                            : "Metro project: unverified\n"
                    : "")
            << "project status: " << projectStatus << '\n';
  if (!nextAction.empty()) {
    std::cout << "next: " << nextAction << '\n';
  }
  std::cout
            << "security: caller bundles and native addons are trusted code; "
               "rnsim is not a sandbox\n";
}

CliOptions parseOptions(int argc, char **argv) {
  CliOptions options;
  std::string metroPlatform = "android";
  std::optional<std::filesystem::path> configPath;
  bool timeoutConfigured = false;
  bool cliBundleSeen = false;
  bool profileConfigured = false;
  bool platformConfigured = false;
  bool viewportConfigured = false;
  const auto addMetroBundle = [&options](const std::string &platform) {
    const auto cwd = std::filesystem::current_path();
    const auto path = defaultMetroBundlePath(cwd, platform);
    options.bundles.push_back({
        .source = "http://localhost:8081/" + path + "?platform=" + platform +
            "&dev=true&minify=false",
        .http = true,
        .defaultMetro = true,
    });
  };
  for (int scan = 1; scan < argc; ++scan) {
    if (std::string(argv[scan]) == "--config") {
      if (++scan >= argc) {
        throw std::invalid_argument("--config requires a value");
      }
      configPath = std::filesystem::path(argv[scan]);
    }
  }
  if (!configPath) {
    const auto defaultPath = std::filesystem::current_path() / "rnsim.json";
    if (std::filesystem::exists(defaultPath)) {
      configPath = defaultPath;
    }
  }
  if (configPath) {
    if (!std::filesystem::exists(*configPath)) {
      throw std::invalid_argument(
          "Config file does not exist: " + configPath->string());
    }
    const auto config = loadSimulatorConfig(
        std::filesystem::weakly_canonical(*configPath));
    metroPlatform = config.platform;
    platformConfigured = true;
    options.runtime.appKey = config.appKey;
    if (config.initialPropsJson) {
      options.runtime.initialPropsJson = *config.initialPropsJson;
    }
    if (config.bundle) {
      options.bundles.push_back(
          {.source = config.bundle->string(), .http = false});
    }
    for (const auto& addon : config.addons) {
      if (addon.name) {
        options.addons.push_back(
            {.token = *addon.name, .origin = rns::AddonRequestOrigin::Config});
      } else if (addon.path) {
        options.addons.push_back({
            .token = addon.path->string(),
            .origin = rns::AddonRequestOrigin::Config,
        });
      }
    }
    options.disabledAddons.insert(
        options.disabledAddons.end(),
        config.disabledAddons.begin(),
        config.disabledAddons.end());
    options.autoAddons = config.autoAddons;
    if (config.viewportWidth) {
      options.runtime.viewportWidth = *config.viewportWidth;
      viewportConfigured = true;
    }
    if (config.viewportHeight) {
      options.runtime.viewportHeight = *config.viewportHeight;
      viewportConfigured = true;
    }
    if (config.pointScaleFactor) {
      options.runtime.pointScaleFactor = *config.pointScaleFactor;
      viewportConfigured = true;
    }
    if (config.fontDirectory) {
      options.runtime.fontDirectory = *config.fontDirectory;
    }
    options.runtime.colorScheme = config.environment.colorScheme;
    options.runtime.appState = config.environment.appState;
    options.runtime.reduceMotion = config.environment.reduceMotion;
    options.runtime.invertColors = config.environment.invertColors;
    options.runtime.highTextContrast = config.environment.highTextContrast;
    options.runtime.screenReader = config.environment.screenReader;
    options.runtime.accessibilityService =
        config.environment.accessibilityService;
    options.runtime.grayscale = config.environment.grayscale;
    options.runtime.boldText = config.environment.boldText;
    options.runtime.reduceTransparency = config.environment.reduceTransparency;
    options.runtime.darkerSystemColors = config.environment.darkerSystemColors;
    options.runtime.orientation = config.environment.orientation;
  }

  int index = 1;
  if (index < argc) {
    const std::string command = argv[index];
    if (command == "interactive") {
      ++index;
    } else if (command == "headless") {
      options.mode = CliOptions::Mode::Headless;
      ++index;
    } else if (command == "test" || command == "conformance") {
      throw std::invalid_argument(
          "Conformance is not available during Nightly. Use headless with "
          "explicit runtime requirements; no result from that mode is a "
          "certification verdict.");
    } else if (!command.starts_with("--")) {
      throw std::invalid_argument("Unknown command: " + command);
    }
  }
  while (index < argc) {
    const std::string name = argv[index++];
    if (!name.starts_with("--")) {
      throw std::invalid_argument("Unexpected positional argument: " + name);
    }
    if (name == "--devtools") {
      bool enabled = true;
      if (index < argc) {
        const std::string candidate = argv[index];
        if (candidate == "true" || candidate == "false" ||
            candidate == "1" || candidate == "0") {
          enabled = parseBoolean(name, candidate);
          ++index;
        }
      }
      options.runtime.devTools.enabled = enabled;
      options.runtime.devTools.open = enabled;
      options.runtime.devTools.waitForDisconnect = false;
      continue;
    }
    if (name == "--no-open") {
      options.runtime.devTools.enabled = true;
      options.runtime.devTools.open = false;
      options.runtime.devTools.waitForDisconnect = false;
      continue;
    }
    if (name == "--no-auto-addons") {
      options.autoAddons = false;
      continue;
    }
    if (name == "--config") {
      if (index >= argc) {
        throw std::invalid_argument("--config requires a value");
      }
      ++index;
      continue;
    }
    if (index >= argc) {
      throw std::invalid_argument(name + " requires a value");
    }
    const std::string value = argv[index++];
    if (name == "--iterations") {
      options.runtime.iterations = std::stoi(value);
    } else if (name == "--timeout-ms") {
      options.runtime.timeoutMs = std::stoi(value);
      timeoutConfigured = true;
    } else if (name == "--settle-ms") {
      options.runtime.settleMs = std::stoi(value);
    } else if (name == "--seed") {
      options.runtime.seed = std::stoi(value);
    } else if (name == "--workload") {
      options.runtime.workload = value;
    } else if (name == "--output") {
      options.outputPath = value;
    } else if (name == "--screenshot") {
      options.screenshotPath = value;
    } else if (name == "--android-font-dir") {
      options.runtime.fontDirectory = value;
    } else if (name == "--inspector-socket") {
      options.runtime.inspectorSocket = value;
    } else if (name == "--trace") {
      options.runtime.tracePath = value;
    } else if (name == "--bundle") {
      if (!cliBundleSeen) {
        options.bundles.clear();
        cliBundleSeen = true;
      }
      options.bundles.push_back({.source = value, .http = false});
    } else if (name == "--bundle-url") {
      if (!cliBundleSeen) {
        options.bundles.clear();
        cliBundleSeen = true;
      }
      options.bundles.push_back({.source = value, .http = true});
    } else if (name == "--url") {
      if (!cliBundleSeen) {
        options.bundles.clear();
        cliBundleSeen = true;
      }
      options.bundles.push_back({.source = value, .http = true});
    } else if (name == "--platform") {
      if (value != "android" && value != "ios") {
        throw std::invalid_argument("--platform must be android or ios");
      }
      metroPlatform = value;
      platformConfigured = true;
    } else if (name == "--app-key") {
      if (value.empty()) {
        throw std::invalid_argument("--app-key must not be empty");
      }
      options.runtime.appKey = value;
    } else if (name == "--initial-props") {
      options.runtime.initialPropsJson = normalizeInitialPropsJson(value);
    } else if (name == "--profile") {
      if (value == "android-rn73") {
        throw std::invalid_argument(
            "profile android-rn73 was removed; the native engine is RN 0.87.0.\n"
            "Use --profile android-rn87 --addon compat-rn73 for the 0.73.x JS adapter.");
      }
      options.runtime.profile = value;
      profileConfigured = true;
    } else if (name == "--viewport-width") {
      options.runtime.viewportWidth = std::stof(value);
      viewportConfigured = true;
    } else if (name == "--viewport-height") {
      options.runtime.viewportHeight = std::stof(value);
      viewportConfigured = true;
    } else if (name == "--point-scale-factor") {
      options.runtime.pointScaleFactor = std::stof(value);
      viewportConfigured = true;
    } else if (name == "--addon") {
      options.addons.push_back(
          {.token = value, .origin = rns::AddonRequestOrigin::Cli});
    } else if (name == "--no-addon") {
      options.disabledAddons.push_back(value);
    } else if (name == "--initial-url") {
      options.initialUrl = value;
    } else if (name == "--require-react-fabric") {
      options.runtime.requireReactFabric = parseBoolean(name, value);
    } else if (name == "--require-no-pending-work") {
      options.runtime.requireNoPendingWork = parseBoolean(name, value);
    } else if (name == "--fail-on-component-fallback") {
      options.runtime.failOnComponentFallback = parseBoolean(name, value);
    } else if (name == "--devtools-port") {
      const auto port = std::stoul(value);
      if (port < 1 || port > 65535) {
        throw std::invalid_argument(
            "devtools-port must be between 1 and 65535");
      }
      options.runtime.devTools.port = static_cast<uint16_t>(port);
    } else if (name == "--devtools-open") {
      options.runtime.devTools.open = parseBoolean(name, value);
    } else if (name == "--devtools-wait-for-debugger-ms") {
      options.runtime.devTools.waitForDebuggerMs = std::stoi(value);
    } else if (name == "--devtools-keep-alive-ms") {
      options.runtime.devTools.keepAliveMs = std::stoi(value);
    } else if (name == "--devtools-frontend-dir") {
      options.runtime.devTools.frontendDirectory = value;
    } else if (name == "--devtools-shell") {
      options.runtime.devTools.shellPath = value;
    } else {
      throw std::invalid_argument("Unknown option: " + name);
    }
  }
  if (profileConfigured) {
    std::optional<std::string> profilePlatform;
    if (options.runtime.profile.rfind("android-", 0) == 0) {
      profilePlatform = "android";
    } else if (options.runtime.profile.rfind("ios-", 0) == 0) {
      profilePlatform = "ios";
    }
    if (profilePlatform) {
      if (platformConfigured && metroPlatform != *profilePlatform) {
        throw std::invalid_argument(
            "--platform " + metroPlatform + " is incompatible with --profile " +
            options.runtime.profile);
      }
      metroPlatform = *profilePlatform;
    }
  } else {
    options.runtime.profile = metroPlatform + "-rn87";
  }
  if (options.mode == CliOptions::Mode::Interactive) {
    options.runtime.mode = rns::SimulatorMode::Interactive;
    // Match the normal React Native host lifecycle: once the caller bundle has
    // registered an unambiguous application, run it immediately. The
    // interactive App panel remains available when multiple keys require a
    // user choice and for switching applications after startup.
    options.runtime.autoRunApplication = true;
    if (options.bundles.empty()) {
      addMetroBundle(metroPlatform);
    }
    if (!timeoutConfigured) {
      options.runtime.timeoutMs = 30000;
    }
    if (!viewportConfigured) {
      options.runtime.viewportWidth = 392.7273f;
      options.runtime.viewportHeight = 753.4545f;
      options.runtime.pointScaleFactor = 2.75f;
    }
  } else if (options.mode == CliOptions::Mode::Conformance) {
    options.runtime.mode = rns::SimulatorMode::Conformance;
  }
  if (options.mode != CliOptions::Mode::Interactive &&
      options.bundles.empty()) {
    throw std::invalid_argument(
        "headless and test require --bundle, --url, or rnsim.json bundle");
  }
  if (options.runtime.iterations < 2 || options.runtime.iterations > 100000) {
    throw std::invalid_argument("iterations must be between 2 and 100000");
  }
  if (options.runtime.timeoutMs < 1 || options.runtime.timeoutMs > 600000) {
    throw std::invalid_argument("timeout-ms must be between 1 and 600000");
  }
  if (options.runtime.settleMs < 0 || options.runtime.settleMs > 60000) {
    throw std::invalid_argument("settle-ms must be between 0 and 60000");
  }
  if (options.runtime.devTools.waitForDebuggerMs < 0 ||
      options.runtime.devTools.waitForDebuggerMs > 600000 ||
      options.runtime.devTools.keepAliveMs < 0 ||
      options.runtime.devTools.keepAliveMs > 600000) {
    throw std::invalid_argument(
        "DevTools wait durations must be between 0 and 600000ms");
  }
  if (options.runtime.viewportWidth <= 0 ||
      options.runtime.viewportWidth > 10000 ||
      options.runtime.viewportHeight <= 0 ||
      options.runtime.viewportHeight > 10000) {
    throw std::invalid_argument(
        "viewport dimensions must be between 0 and 10000 dp");
  }
  if (options.runtime.pointScaleFactor <= 0 ||
      options.runtime.pointScaleFactor > 10) {
    throw std::invalid_argument("point-scale-factor must be between 0 and 10");
  }
  if (options.runtime.workload.empty() ||
      !std::all_of(options.runtime.workload.begin(),
                   options.runtime.workload.end(), [](char value) {
                     return std::isalnum(static_cast<unsigned char>(value)) ||
                            value == '-' || value == '_';
                   })) {
    throw std::invalid_argument("workload must be an identifier");
  }
#if !RNS_ENABLE_SKIA
  if (options.screenshotPath) {
    throw std::invalid_argument(
        "--screenshot requires a build configured with RNS_ENABLE_SKIA=ON");
  }
#endif
  return options;
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc >= 2 && std::string(argv[1]) == "--list-addons") {
      bool json = argc == 3 && std::string(argv[2]) == "--json";
      if (argc > 3 || (argc == 3 && !json)) {
        throw std::invalid_argument("Usage: rnsim --list-addons [--json]");
      }
      const auto catalog = rns::builtinAddonCatalogJson();
      if (json) {
        std::cout << folly::toJson(catalog) << '\n';
      } else {
        for (const auto& addon : catalog["addons"]) {
          std::cout << addon["name"].asString() << " auto="
                    << addon["auto"].asString() << '\n';
        }
      }
      return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "--version") {
      if (argc > 3 || (argc == 3 && std::string(argv[2]) != "--json")) {
        throw std::invalid_argument("Usage: rnsim --version [--json]");
      }
      printVersion(argc == 3);
      return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "doctor") {
      bool json = false;
      bool help = false;
      bool requestedCompatRn73 = false;
      std::optional<std::string> metroUrl;
      for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--json") {
          json = true;
        } else if (argument == "--help" || argument == "-h") {
          help = true;
        } else if (argument == "--url") {
          if (++index >= argc) {
            throw std::invalid_argument("--url requires a value");
          }
          metroUrl = argv[index];
        } else if (argument == "--addon") {
          if (++index >= argc) {
            throw std::invalid_argument("--addon requires a value");
          }
          requestedCompatRn73 =
              requestedCompatRn73 || argv[index] == std::string("compat-rn73");
        } else {
          throw std::invalid_argument(
              "Usage: rnsim doctor [--json] [--url URL] [--addon compat-rn73]");
        }
      }
      if (help) {
        std::cout
            << "Usage: rnsim doctor [--json] [--url URL] [--addon compat-rn73]\n";
        return 0;
      }
      printDoctor(argv[0], json, metroUrl, requestedCompatRn73);
      return 0;
    }
    if (argc == 2 && (std::string(argv[1]) == "--help" ||
                      std::string(argv[1]) == "-h" ||
                      std::string(argv[1]) == "help")) {
      printHelp();
      return 0;
    }
    if (argc == 3 &&
        (std::string(argv[2]) == "--help" ||
         std::string(argv[2]) == "-h") &&
        (std::string(argv[1]) == "interactive" ||
         std::string(argv[1]) == "headless" ||
         std::string(argv[1]) == "test" ||
         std::string(argv[1]) == "conformance")) {
      printHelp();
      return 0;
    }
    auto options = parseOptions(argc, argv);
    const auto launchRoot = std::filesystem::current_path();
    if (options.mode == CliOptions::Mode::Interactive &&
        !options.runtime.appKey) {
      options.runtime.appKey = inferAppKeyFromProject(launchRoot);
    }
    if (!options.initialUrl) {
      if (const char* env = std::getenv("RNSIM_INITIAL_URL")) {
        options.initialUrl = env;
      }
    }
    const auto expoFromProjectOrUrl =
        [](const std::optional<std::filesystem::path> &project,
           const std::string &url) {
          return bundleUrlLooksLikeExpo(url) ||
              (project && detectExpoProject(*project).detected);
        };
    rns::LaunchDraft draft(std::move(options.runtime));
    if (detectExpoProject(launchRoot).detected) {
      draft.setProjectKind(rns::ProjectKind::Expo);
    }
    draft.setAutoAddons(options.autoAddons);
    for (const auto& name : options.disabledAddons) {
      draft.disableAddon(name);
    }
    for (const auto& addon : options.addons) {
      if (rns::looksLikeAddonModulePath(addon.token)) {
        draft.addAddonPath(addon.token, addon.origin);
      } else {
        draft.addBuiltInAddon(addon.token, addon.origin);
      }
    }
    if (options.initialUrl) {
      draft.setInitialUrl(options.initialUrl);
    }
    auto candidates = rns::prepareExplicitAddons(draft);
    const auto addLocalBundle = [&](const CliOptions::BundleSource& bundle,
                                    std::optional<std::string> body) {
      rns::InitialBundleSpec spec;
      spec.sourceUrl = body ? bundle.source
                            : std::string("file://") +
              std::filesystem::weakly_canonical(bundle.source).generic_string();
      if (body) {
        spec.body = std::move(*body);
      } else {
        spec.path = std::filesystem::path(bundle.source);
      }
      draft.addBundle(std::move(spec));
    };
    if (options.mode == CliOptions::Mode::Interactive) {
      std::cerr << "starting interactive session ("
                << options.runtime.profile << ", viewport "
                << options.runtime.viewportWidth << "x"
                << options.runtime.viewportHeight << ")\n";
    }
    const auto fontDirectory = options.runtime.fontDirectory.value_or(
        std::filesystem::path{});
    rns::EngineResult result;
    if (options.mode == CliOptions::Mode::Interactive) {
#if RNS_ENABLE_IMGUI
      rns::Engine runtime;
      auto prepareRuntime = [&](
                                const std::function<bool()> &cancelled) {
        std::vector<std::optional<std::string>> httpBodies(
            options.bundles.size());
        for (size_t index = 0; index < options.bundles.size(); ++index) {
          auto &bundle = options.bundles[index];
          if (cancelled()) {
            return;
          }
          if (!bundle.http) {
            continue;
          }
          std::cerr << "loading Metro bundle " << bundle.source << '\n';
          try {
            if (!waitForMetro(bundle.source, cancelled)) {
              return;
            }
            std::optional<std::filesystem::path> metroProject;
            httpBodies[index] = fetchDefaultMetroBundle(
                bundle,
                nullptr,
                cancelled,
                &metroProject);
            if (expoFromProjectOrUrl(metroProject, bundle.source)) {
              draft.setProjectKind(rns::ProjectKind::Expo);
            }
            if (cancelled()) {
              return;
            }
          } catch (const HttpRequestCancelled &) {
            return;
          } catch (const std::exception &error) {
            throw rns::RetryableNetworkError(
                "Cannot load the bundle from " + bundle.source +
                ". Start Metro or pass --url/--bundle: " + error.what());
          }
        }
        if (cancelled()) {
          return;
        }
        for (size_t index = 0; index < options.bundles.size(); ++index) {
          const auto &bundle = options.bundles[index];
          addLocalBundle(bundle, httpBodies[index]);
        }
        auto plan = rns::finalizeLaunchPlan(
            std::move(draft), std::move(candidates));
        runtime.applyLaunchPlan(std::move(plan));
      };
      result = rns::runInteractiveFrontend(
          runtime, fontDirectory, std::move(prepareRuntime));
#else
      throw std::runtime_error(
          "interactive mode requires RNS_ENABLE_IMGUI=ON and RNS_ENABLE_SKIA=ON");
#endif
    } else {
      std::vector<std::optional<std::string>> httpBodies(
          options.bundles.size());
      for (size_t index = 0; index < options.bundles.size(); ++index) {
        auto &bundle = options.bundles[index];
        if (!bundle.http) {
          continue;
        }
        std::cerr << "loading bundle " << bundle.source << '\n';
        try {
          std::optional<std::filesystem::path> metroProject;
          httpBodies[index] = fetchDefaultMetroBundle(
              bundle, &draft.config(), {}, &metroProject);
          if (expoFromProjectOrUrl(metroProject, bundle.source)) {
            draft.setProjectKind(rns::ProjectKind::Expo);
          }
        } catch (const std::exception &error) {
          throw rns::RetryableNetworkError(
              "Cannot load the bundle from " + bundle.source + ": " +
              error.what());
        }
      }
      for (size_t index = 0; index < options.bundles.size(); ++index) {
        addLocalBundle(options.bundles[index], httpBodies[index]);
      }
      auto plan = rns::finalizeLaunchPlan(
          std::move(draft), std::move(candidates));
      rns::Engine runtime;
      runtime.applyLaunchPlan(std::move(plan));
      result = runtime.run();
    }
    if (!result.metricsJson.empty()) {
#if RNS_ENABLE_SKIA
      if (options.screenshotPath) {
        if (result.scene == nullptr) {
          throw std::runtime_error("Engine returned no retained scene");
        }
        const auto image = rns::exportSceneToPng(
            *result.scene, *options.screenshotPath,
            fontDirectory);
        std::cerr << "wrote screenshot " << *options.screenshotPath << " ("
                  << image.width << 'x' << image.height << ")\n";
      }
#endif
      // Interactive already has the live window; the metrics document includes
      // both Fabric trees and is a headless/Inspector payload, not a CLI log.
      if (options.mode != CliOptions::Mode::Interactive) {
        std::cout << result.metricsJson;
      }
      if (options.outputPath) {
        std::ofstream output(*options.outputPath, std::ios::binary);
        if (!output) {
          throw std::runtime_error("Cannot open output file: " +
                                   options.outputPath->string());
        }
        output << result.metricsJson;
      }
    }
    if (!result.error.empty()) {
      std::cerr << "rnsim failed: " << result.error << '\n';
    }
    return result.exitCode;
  } catch (const std::exception &error) {
    std::cerr << "rnsim failed: " << error.what() << '\n';
    return 1;
  }
}
