#include <react-native-simulator/Engine.h>
#include <react-native-simulator/SimulatorAddon.h>

#include "HttpBundleLoader.h"
#include "SimulatorConfig.h"

#if RNS_ENABLE_SKIA
#include "SkiaPngExporter.h"
#endif
#if RNS_ENABLE_IMGUI
#include "InteractiveFrontend.h"
#endif

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <folly/json.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
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
  std::vector<std::string> addons;
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
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
    sourceFile.resize(dot);
  }
  return sourceFile + ".bundle";
}

std::string appKeyFromEntryFile(const std::string &sourceFile) {
  auto stem = std::filesystem::path(sourceFile).stem().string();
  for (const char *suffix : {".android", ".ios", ".native", ".web"}) {
    const auto size = std::char_traits<char>::length(suffix);
    if (stem.size() > size &&
        stem.compare(stem.size() - size, size, suffix) == 0) {
      stem.resize(stem.size() - size);
      break;
    }
  }
  return stem;
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
    if ((*main)[0] != '/' && main->find("://") == std::string::npos) {
      addEntryCandidate(entries, seen, root, *main);
    }
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
    const std::filesystem::path &root,
    const std::string &platform) {
  if (auto name = appJsonName(root)) {
    return name;
  }
  const auto entries = discoverMetroEntries(root, platform);
  if (!entries.empty()) {
    auto key = appKeyFromEntryFile(entries.front());
    if (!key.empty()) {
      return key;
    }
  }
  return std::nullopt;
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

void waitForMetro(const std::string &bundleUrl) {
  if (metroStatusRunning(bundleUrl)) {
    return;
  }
  const auto origin = bundleOrigin(bundleUrl);
  std::cerr << "waiting for Metro at " << origin << '\n'
            << "start it with npm start; rnsim does not launch Metro\n";
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (!metroStatusRunning(bundleUrl)) {
    if (std::chrono::steady_clock::now() >= deadline) {
      throw std::runtime_error(
          "Timed out waiting for Metro at " + origin +
          ". Start Metro or pass --url/--bundle.");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }
  std::cerr << "connected to Metro\n";
}

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
    rns::EngineConfig &runtime) {
  try {
    return fetchHttpBundle(bundle.source);
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
        auto bytes = fetchHttpBundle(fallback);
        bundle.source = std::move(fallback);
        if (!runtime.appKey) {
          runtime.appKey = inferAppKeyFromProject(*project, platform);
        }
        return bytes;
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
  rnsim doctor [--json]         Diagnose this installation
  rnsim --version [--json]      Print build and runtime contract versions

Common options:
  --config FILE           Local config (default: ./rnsim.json when present)
  --platform android|ios  Metro target platform (default: android)
  --profile NAME          RN contract: android-rn87, ios-rn87, android-rn73
  --bundle FILE           Load a caller-built local bundle
  --android-font-dir DIR  Fonts used by Skia measurement and paint

Interactive options:
  --url URL               Complete loopback bundle URL
  --app-key NAME          AppRegistry key (preselected in the interactive launcher)
  --initial-props JSON    JSON object passed as runApplication initialProps
  --devtools              Enable React Native DevTools (frontend may be external)
  --no-open               Enable DevTools without opening its frontend
  --devtools-frontend-dir DIR  Compatible external DevTools frontend

With no --bundle/--url, interactive mode waits for Metro at localhost:8081
and loads index.bundle. If Metro has no ./index, rnsim reads the packager
project path from the error and tries entry files found there. app.json name
becomes --app-key.

Headless options:
  --iterations N          Workload size
  --timeout-ms N          Workload completion timeout
  --output FILE           Persist metrics JSON
  --trace FILE            Persist Chrome Trace JSON
  --screenshot FILE       Persist the retained Skia scene as PNG
  --devtools              Inspect the workload with React Native DevTools

Interactive mode does not require RN$SimulatorWorkload signals;
headless mode remains strict and finite. Public conformance is intentionally
disabled in v0.1.0 until canonical profile/font/oracle manifests are complete.
)";
}

folly::dynamic buildInformation() {
  struct utsname system{};
  const bool unameOk = ::uname(&system) == 0;
  return folly::dynamic::object
      ("name", "react-native-simulator")
      ("version", RNS_PROJECT_VERSION)
      ("commit", RNS_BUILD_COMMIT)
      ("dirty", std::string(RNS_BUILD_DIRTY) == "true")
      ("reactNative", RNS_REACT_NATIVE_VERSION)
      ("hermes", RNS_HERMES_VERSION)
      ("addonAbi", rns::kSimulatorAddonAbiVersion)
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
            << ", addon ABI " << info["addonAbi"].asInt() << '\n'
            << "macOS " << info["minimumMacOS"].asString()
            << "+ arm64; interactive="
            << (info["features"]["interactive"].asBool() ? "yes" : "no")
            << ", Skia="
            << (info["features"]["skia"].asBool() ? "yes" : "no") << '\n';
}

void printDoctor(const char* argv0, bool json) {
  auto report = buildInformation();
  std::error_code error;
  auto executable = std::filesystem::weakly_canonical(argv0, error);
  if (error) executable = std::filesystem::absolute(argv0, error);
  const auto installRoot = executable.parent_path().parent_path();
  const auto frontend = installRoot /
      "share/react-native-simulator/debugger-frontend/rn_fusebox.html";
  const auto localConfig = std::filesystem::current_path() / "rnsim.json";
  report["executable"] = executable.string();
  report["installedDevToolsFrontend"] =
      std::filesystem::is_regular_file(frontend);
  report["localConfig"] = std::filesystem::is_regular_file(localConfig)
      ? folly::dynamic(localConfig.string())
      : folly::dynamic(nullptr);
  report["defaultProfile"] = "android-rn87";
  report["status"] = "experimental-android-first";
  report["securitySandbox"] = false;
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
  bool viewportConfigured = false;
  const auto addMetroBundle = [&options](const std::string &platform) {
    const auto cwd = std::filesystem::current_path();
    std::string path = "index.bundle";
    const bool hasRootIndex =
        std::filesystem::exists(cwd / "index.js") ||
        std::filesystem::exists(cwd / ("index." + platform + ".js"));
    if (!hasRootIndex) {
      const auto entries = discoverMetroEntries(cwd, platform);
      if (!entries.empty()) {
        path = toBundlePath(entries.front());
      }
    }
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
    options.runtime.appKey = config.appKey;
    if (config.initialPropsJson) {
      options.runtime.initialPropsJson = *config.initialPropsJson;
    }
    if (config.bundle) {
      options.bundles.push_back(
          {.source = config.bundle->string(), .http = false});
    }
    for (const auto& addon : config.addons) {
      options.addons.push_back(addon.string());
    }
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
          "Conformance is not available in v0.1.0. Use headless with explicit "
          "runtime requirements; no result from that mode is a certification verdict.");
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
    } else if (name == "--app-key") {
      if (value.empty()) {
        throw std::invalid_argument("--app-key must not be empty");
      }
      options.runtime.appKey = value;
    } else if (name == "--initial-props") {
      options.runtime.initialPropsJson = normalizeInitialPropsJson(value);
    } else if (name == "--profile") {
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
      options.addons.push_back(value);
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
  if (!profileConfigured) {
    options.runtime.profile = metroPlatform + "-rn87";
  }
  if (options.mode == CliOptions::Mode::Interactive) {
    options.runtime.mode = rns::SimulatorMode::Interactive;
    options.runtime.autoRunApplication = false;
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
    if (argc >= 2 && std::string(argv[1]) == "--version") {
      if (argc > 3 || (argc == 3 && std::string(argv[2]) != "--json")) {
        throw std::invalid_argument("Usage: rnsim --version [--json]");
      }
      printVersion(argc == 3);
      return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "doctor") {
      if (argc > 3 ||
          (argc == 3 && std::string(argv[2]) != "--json" &&
           std::string(argv[2]) != "--help" &&
           std::string(argv[2]) != "-h")) {
        throw std::invalid_argument("Usage: rnsim doctor [--json]");
      }
      if (argc == 3 && (std::string(argv[2]) == "--help" ||
                        std::string(argv[2]) == "-h")) {
        std::cout << "Usage: rnsim doctor [--json]\n";
        return 0;
      }
      printDoctor(argv[0], argc == 3);
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
    if (options.mode == CliOptions::Mode::Interactive &&
        !options.runtime.appKey) {
      const auto platform =
          options.runtime.profile.rfind("ios", 0) == 0 ? "ios" : "android";
      options.runtime.appKey =
          inferAppKeyFromProject(std::filesystem::current_path(), platform);
    }
    if (options.mode == CliOptions::Mode::Interactive) {
      std::cerr << "starting interactive session ("
                << options.runtime.profile << ", viewport "
                << options.runtime.viewportWidth << "x"
                << options.runtime.viewportHeight << ")\n";
    }
    std::vector<std::optional<std::string>> httpBodies(options.bundles.size());
    for (size_t index = 0; index < options.bundles.size(); ++index) {
      auto &bundle = options.bundles[index];
      if (!bundle.http) {
        continue;
      }
      std::cerr << "loading Metro bundle " << bundle.source << '\n';
      try {
        if (options.mode == CliOptions::Mode::Interactive) {
          waitForMetro(bundle.source);
        }
        httpBodies[index] = fetchDefaultMetroBundle(bundle, options.runtime);
      } catch (const std::exception &error) {
        throw std::runtime_error(
            "Cannot load the bundle from " + bundle.source +
            ". Start Metro or pass --url/--bundle: " + error.what());
      }
    }
    rns::Engine runtime(std::move(options.runtime));
    for (const auto &addon : options.addons) {
      runtime.addAddon(addon);
    }
    for (size_t index = 0; index < options.bundles.size(); ++index) {
      const auto &bundle = options.bundles[index];
      if (bundle.http) {
        runtime.loadBundle(*httpBodies[index], bundle.source);
      } else {
        runtime.loadBundle(std::filesystem::path(bundle.source));
      }
    }
    const auto result = options.mode == CliOptions::Mode::Interactive
#if RNS_ENABLE_IMGUI
        ? rns::runInteractiveFrontend(
              runtime,
              options.runtime.fontDirectory.value_or(std::filesystem::path{}))
#else
        ? throw std::runtime_error(
              "interactive mode requires RNS_ENABLE_IMGUI=ON and RNS_ENABLE_SKIA=ON")
#endif
        : runtime.run();
    if (!result.metricsJson.empty()) {
#if RNS_ENABLE_SKIA
      if (options.screenshotPath) {
        if (result.scene == nullptr) {
          throw std::runtime_error("Engine returned no retained scene");
        }
        const auto image = rns::exportSceneToPng(
            *result.scene, *options.screenshotPath,
            options.runtime.fontDirectory.value_or(std::filesystem::path{}));
        std::cerr << "wrote screenshot " << *options.screenshotPath << " ("
                  << image.width << 'x' << image.height << ")\n";
      }
#endif
      std::cout << result.metricsJson;
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
