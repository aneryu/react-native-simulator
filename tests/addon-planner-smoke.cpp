#include <react-native-simulator/Engine.h>
#include <react-native-simulator/SimulatorAddon.h>

#include "EngineTestSupport.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rns = ReactNativeSimulator;

namespace {
struct HookCounts {
  int bind{0};
  int unbind{0};
  int configure{0};
  int install{0};
  int quiesce{0};
  int destroyed{0};
};

class RecordingAddon final : public rns::SimulatorAddon {
 public:
  explicit RecordingAddon(
      std::string name,
      std::shared_ptr<HookCounts> counts,
      rns::AddonManifest extra = {})
      : name_(std::move(name)),
        counts_(std::move(counts)),
        extra_(std::move(extra)) {}
  ~RecordingAddon() override { ++counts_->destroyed; }

  rns::AddonManifest manifest() const override {
    rns::AddonManifest manifest = extra_;
    manifest.name = name_;
    if (manifest.addonVersion.empty()) {
      manifest.addonVersion = "0.0.1";
    }
    return manifest;
  }
  void bind(const rns::AddonHost&) override { ++counts_->bind; }
  void unbind() noexcept override { ++counts_->unbind; }
  std::shared_ptr<facebook::react::TurboModule> getTurboModule(
      const rns::AddonGenerationContext&,
      facebook::jsi::Runtime&,
      const std::string&,
      const std::shared_ptr<facebook::react::CallInvoker>&) override {
    return nullptr;
  }
  std::shared_ptr<facebook::react::TurboModule> wrapTurboModule(
      const rns::AddonGenerationContext&,
      facebook::jsi::Runtime&,
      const std::string&,
      std::shared_ptr<facebook::react::TurboModule> framework,
      const std::shared_ptr<facebook::react::CallInvoker>&) override {
    return framework;
  }
  void configureFabric(
      const rns::AddonGenerationContext&,
      rns::AddonFabricRegistrar&) override {
    ++counts_->configure;
  }
  void installJSI(
      const rns::AddonGenerationContext&,
      facebook::jsi::Runtime&,
      const std::shared_ptr<facebook::react::CallInvoker>&) override {
    ++counts_->install;
  }
  void hostSnapshotChanged(const rns::AddonHostSnapshot&) override {}
  void quiesceGeneration(std::uint64_t) noexcept override { ++counts_->quiesce; }

 private:
  std::string name_;
  std::shared_ptr<HookCounts> counts_;
  rns::AddonManifest extra_;
};

rns::EngineResult runDraft(rns::LaunchDraft draft) {
  draft.addBundle(rns::test::memoryBundle(
      "RN$SimulatorWorkload.ready();\n"
      "globalThis.RN$SimulatorWorkloadResult={iterations:1,checksum:1};\n"
      "RN$SimulatorWorkload.complete();\n",
      "memory://planner.js"));
  auto candidates = rns::prepareExplicitAddons(draft);
  auto plan = rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
  rns::Engine engine;
  engine.applyLaunchPlan(std::move(plan));
  return engine.run();
}

std::vector<std::string> addonNames(const std::string& metrics) {
  std::vector<std::string> names;
  const auto start = metrics.find("\"addons\":[");
  if (start == std::string::npos) {
    return names;
  }
  size_t depth = 0;
  size_t end = std::string::npos;
  for (size_t i = start + 10; i < metrics.size(); ++i) {
    if (metrics[i] == '[') {
      ++depth;
    } else if (metrics[i] == ']') {
      if (depth == 0) {
        end = i;
        break;
      }
      --depth;
    }
  }
  if (end == std::string::npos) {
    return names;
  }
  const std::string key = "\"name\":\"";
  auto cursor = start;
  while (cursor < end) {
    cursor = metrics.find(key, cursor);
    if (cursor == std::string::npos || cursor > end) {
      break;
    }
    cursor += key.size();
    const auto close = metrics.find('"', cursor);
    names.emplace_back(metrics.substr(cursor, close - cursor));
    cursor = close + 1;
  }
  return names;
}

bool containsName(const std::vector<std::string>& names, const std::string& name) {
  return std::find(names.begin(), names.end(), name) != names.end();
}

void expectThrows(const char* label, const std::function<void()>& fn, const char* needle) {
  try {
    fn();
    throw std::runtime_error(std::string(label) + " did not throw");
  } catch (const rns::TerminalLaunchPlanError& error) {
    if (std::string(error.what()).find(needle) == std::string::npos) {
      throw std::runtime_error(
          std::string(label) + " threw '" + error.what() + "' missing '" +
          needle + "'");
    }
  }
}

bool uniqueContractNames(const std::string& metrics, const char* arrayKey) {
  const std::string key = std::string("\"") + arrayKey + "\":[";
  const auto start = metrics.find(key);
  if (start == std::string::npos) {
    return false;
  }
  size_t depth = 0;
  size_t end = std::string::npos;
  for (size_t i = start + key.size(); i < metrics.size(); ++i) {
    if (metrics[i] == '[') {
      ++depth;
    } else if (metrics[i] == ']') {
      if (depth == 0) {
        end = i;
        break;
      }
      --depth;
    }
  }
  if (end == std::string::npos) {
    return false;
  }
  std::unordered_set<std::string> names;
  const std::string nameKey = "\"name\":\"";
  auto cursor = start;
  while (cursor < end) {
    cursor = metrics.find(nameKey, cursor);
    if (cursor == std::string::npos || cursor > end) {
      break;
    }
    cursor += nameKey.size();
    const auto close = metrics.find('"', cursor);
    if (!names.insert(metrics.substr(cursor, close - cursor)).second) {
      return false;
    }
    cursor = close + 1;
  }
  return !names.empty();
}

int countName(const std::string& metrics, const std::string& name) {
  int count = 0;
  const std::string needle = "\"name\":\"" + name + "\"";
  for (size_t pos = 0; (pos = metrics.find(needle, pos)) != std::string::npos;
       pos += needle.size()) {
    ++count;
  }
  return count;
}
} // namespace

int main(int argc, char** argv) {
  try {
    rns::EngineConfig config;
    config.iterations = 1;
    config.timeoutMs = 1000;
    config.profile = "android-rn87";

    {
      rns::LaunchDraft draft(config);
      const auto result = runDraft(std::move(draft));
      const auto names = addonNames(result.metricsJson);
      if (result.exitCode != 0 || !containsName(names, "safe-area") ||
          containsName(names, "expo") || containsName(names, "compat-rn73")) {
        std::cerr << "auto-only plain embedder failed\n"
                  << result.metricsJson << '\n';
        return 1;
      }
      if (result.metricsJson.find("\"name\":\"RootView\"") == std::string::npos ||
          !uniqueContractNames(result.metricsJson, "modules") ||
          !uniqueContractNames(result.metricsJson, "components") ||
          countName(result.metricsJson, "RootView") < 1) {
        std::cerr << "inventory contract uniqueness / RootView failed\n"
                  << result.metricsJson << '\n';
        return 1;
      }
    }

    {
      rns::LaunchDraft draft(config);
      draft.setProjectKind(rns::ProjectKind::Expo);
      const auto result = runDraft(std::move(draft));
      const auto names = addonNames(result.metricsJson);
      if (result.exitCode != 0 || names.size() < 2 || names[0] != "expo" ||
          names[1] != "safe-area") {
        std::cerr << "expo auto order failed\n" << result.metricsJson << '\n';
        return 1;
      }
    }

    {
      rns::LaunchDraft draft(config);
      draft.addBuiltInAddon("compat-rn73", rns::AddonRequestOrigin::Embedder);
      const auto result = runDraft(std::move(draft));
      const auto names = addonNames(result.metricsJson);
      if (result.exitCode != 0 || !containsName(names, "safe-area") ||
          !containsName(names, "compat-rn73") ||
          result.metricsJson.find("\"targetFamily\":\"0.73.x\"") ==
              std::string::npos) {
        std::cerr << "explicit compat failed\n" << result.metricsJson << '\n';
        return 1;
      }
    }

    {
      rns::LaunchDraft draft(config);
      draft.disableAddon("safe-area");
      const auto result = runDraft(std::move(draft));
      const auto names = addonNames(result.metricsJson);
      if (result.exitCode != 0 || containsName(names, "safe-area")) {
        std::cerr << "disabled auto slot failed\n" << result.metricsJson << '\n';
        return 1;
      }
    }

    {
      rns::LaunchDraft draft(config);
      draft.setAutoAddons(false);
      const auto result = runDraft(std::move(draft));
      const auto names = addonNames(result.metricsJson);
      if (result.exitCode != 0 || containsName(names, "safe-area")) {
        std::cerr << "no-auto-addons failed\n" << result.metricsJson << '\n';
        return 1;
      }
    }

    expectThrows(
        "unknown bare name",
        [&] {
          rns::LaunchDraft draft(config);
          draft.addBuiltInAddon("does-not-exist");
          (void)rns::prepareExplicitAddons(draft);
        },
        "unknown addon name");

    expectThrows(
        "unknown disabled name",
        [&] {
          rns::LaunchDraft draft(config);
          draft.disableAddon("does-not-exist");
          auto candidates = rns::prepareExplicitAddons(draft);
          (void)rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
        },
        "unknown addon name");

    expectThrows(
        "explicit/disabled contradiction",
        [&] {
          rns::LaunchDraft draft(config);
          draft.addBuiltInAddon("compat-rn73");
          draft.disableAddon("compat-rn73");
          auto candidates = rns::prepareExplicitAddons(draft);
          (void)rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
        },
        "also disabled");

    expectThrows(
        "two explicit same names",
        [&] {
          rns::LaunchDraft draft(config);
          draft.addBuiltInAddon("compat-rn73");
          draft.addBuiltInAddon("compat-rn73");
          (void)rns::prepareExplicitAddons(draft);
        },
        "duplicate addon name");

    expectThrows(
        "compat on ios",
        [&] {
          rns::EngineConfig ios = config;
          ios.profile = "ios-rn87";
          rns::LaunchDraft draft(ios);
          draft.addBuiltInAddon("compat-rn73");
          auto candidates = rns::prepareExplicitAddons(draft);
          (void)rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
        },
        "allows profiles");

    expectThrows(
        "compat on macos",
        [&] {
          rns::EngineConfig macos = config;
          macos.profile = "macos-rn87";
          rns::LaunchDraft draft(macos);
          draft.addBuiltInAddon("compat-rn73");
          auto candidates = rns::prepareExplicitAddons(draft);
          (void)rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
        },
        "allows profiles");

    expectThrows(
        "fixed-point RCTView",
        [&] {
          rns::LaunchDraft draft(config);
          rns::AddonManifest extra;
          extra.components = {{
              "RCTView",
              rns::RuntimeCapabilityClass::HostAdapted,
              rns::AddonComponentKind::FabricDescriptor,
              {},
              {},
              "alias",
          }};
          draft.addAddon(
              std::make_unique<RecordingAddon>(
                  "alias-view", std::make_shared<HookCounts>(), extra),
              "alias-view",
              rns::AddonRequestOrigin::Test);
          auto candidates = rns::prepareExplicitAddons(draft);
          (void)rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
        },
        "fixed point");

    expectThrows(
        "second compatibility claim",
        [&] {
          rns::LaunchDraft draft(config);
          draft.addBuiltInAddon("compat-rn73");
          rns::AddonManifest extra;
          extra.bundleCompatibility = {{
              .targetFamily = "0.72.x",
              .jsVisibleReactNativeVersion = "0.72.0",
              .level = "best-effort-source-js",
          }};
          draft.addAddon(
              std::make_unique<RecordingAddon>(
                  "other-compat", std::make_shared<HookCounts>(), extra),
              "other-compat",
              rns::AddonRequestOrigin::Test);
          auto candidates = rns::prepareExplicitAddons(draft);
          (void)rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
        },
        "bundleCompatibility");

    {
      rns::LaunchDraft draft(config);
      auto counts = std::make_shared<HookCounts>();
      draft.addAddon(
          std::make_unique<RecordingAddon>("probe-addon", counts),
          "probe-addon",
          rns::AddonRequestOrigin::Test);
      const auto result = runDraft(std::move(draft));
      if (result.exitCode != 0 || counts->bind != 1 || counts->unbind != 1 ||
          counts->configure != 1 || counts->install != 1 ||
          counts->quiesce != 1) {
        std::cerr << "successful generation hook counts failed\n";
        return 1;
      }
    }

    {
      rns::LaunchDraft draft(config);
      draft.addAddon(
          std::make_unique<RecordingAddon>(
              "late-kind", std::make_shared<HookCounts>()),
          "late-kind",
          rns::AddonRequestOrigin::Test);
      auto candidates = rns::prepareExplicitAddons(draft);
      draft.setProjectKind(rns::ProjectKind::Expo);
      draft.addBundle(rns::test::memoryBundle(
          "RN$SimulatorWorkload.ready();\n"
          "globalThis.RN$SimulatorWorkloadResult={iterations:1,checksum:1};\n"
          "RN$SimulatorWorkload.complete();\n",
          "memory://late-kind.js"));
      auto plan = rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
      rns::Engine engine;
      engine.applyLaunchPlan(std::move(plan));
      const auto result = engine.run();
      const auto names = addonNames(result.metricsJson);
      if (result.exitCode != 0 || !containsName(names, "expo")) {
        std::cerr << "setProjectKind after prepare did not auto-load expo\n"
                  << result.metricsJson << '\n';
        return 1;
      }
    }

    {
      rns::LaunchDraft draft(config);
      draft.addBuiltInAddon("safe-area", rns::AddonRequestOrigin::Embedder);
      const auto result = runDraft(std::move(draft));
      const auto names = addonNames(result.metricsJson);
      const auto count = static_cast<int>(
          std::count(names.begin(), names.end(), "safe-area"));
      if (result.exitCode != 0 || count != 1) {
        std::cerr << "auto + same-name built-in failed\n"
                  << result.metricsJson << '\n';
        return 1;
      }
    }

    {
      rns::LaunchDraft draft(config);
      draft.addAddon(
          std::make_unique<RecordingAddon>(
              "safe-area", std::make_shared<HookCounts>()),
          "safe-area",
          rns::AddonRequestOrigin::Test);
      const auto result = runDraft(std::move(draft));
      const auto names = addonNames(result.metricsJson);
      const auto count = static_cast<int>(
          std::count(names.begin(), names.end(), "safe-area"));
      if (result.exitCode != 0 || count != 1) {
        std::cerr << "auto + same-name in-process slot failed\n"
                  << result.metricsJson << '\n';
        return 1;
      }
    }

    if (argc > 1) {
      rns::resetAddonPreparationCounters();
      rns::LaunchDraft draft(config);
      draft.addAddonPath(argv[1], rns::AddonRequestOrigin::Test);
      const auto result = runDraft(std::move(draft));
      const auto counters = rns::addonPreparationCounters();
      if (counters.moduleOpens != 1 || counters.moduleCreates != 1 ||
          counters.planFinalizations != 1 || counters.planApplications != 1) {
        std::cerr << "MODULE preparation counters drifted: opens="
                  << counters.moduleOpens << " creates=" << counters.moduleCreates
                  << " finalize=" << counters.planFinalizations
                  << " apply=" << counters.planApplications << '\n';
        return 1;
      }
      const auto names = addonNames(result.metricsJson);
      if (result.exitCode != 0 || !containsName(names, "fabric-probe") ||
          result.metricsJson.find("\"source\":\"module\"") == std::string::npos) {
        std::cerr << "explicit MODULE path failed\n"
                  << result.metricsJson << '\n';
        return 1;
      }

      expectThrows(
          "duplicate canonical path",
          [&] {
            rns::LaunchDraft dup(config);
            dup.addAddonPath(argv[1], rns::AddonRequestOrigin::Test);
            dup.addAddonPath(argv[1], rns::AddonRequestOrigin::Test);
            (void)rns::prepareExplicitAddons(dup);
          },
          "Duplicate addon MODULE path");

      const auto tmp = std::filesystem::temp_directory_path() /
          "rns-addon-symlink-duplicate";
      std::filesystem::remove_all(tmp);
      std::filesystem::create_directories(tmp);
      const auto link = tmp / "alias.so";
      std::filesystem::create_symlink(
          std::filesystem::absolute(argv[1]), link);
      expectThrows(
          "duplicate symlink path",
          [&] {
            rns::LaunchDraft dup(config);
            dup.addAddonPath(argv[1], rns::AddonRequestOrigin::Test);
            dup.addAddonPath(link, rns::AddonRequestOrigin::Test);
            (void)rns::prepareExplicitAddons(dup);
          },
          "Duplicate addon MODULE path");
      std::filesystem::remove_all(tmp);
    }

    if (argc > 2) {
      rns::LaunchDraft draft(config);
      draft.addAddonPath(argv[2], rns::AddonRequestOrigin::Test);
      const auto result = runDraft(std::move(draft));
      const auto names = addonNames(result.metricsJson);
      const auto count = static_cast<int>(
          std::count(names.begin(), names.end(), "safe-area"));
      if (result.exitCode != 0 || count != 1 ||
          result.metricsJson.find("\"source\":\"module\"") == std::string::npos) {
        std::cerr << "auto + same-name MODULE slot failed\n"
                  << result.metricsJson << '\n';
        return 1;
      }
    }

    {
      rns::LaunchDraft draft;
      auto candidates = rns::prepareExplicitAddons(draft);
      rns::Engine engine;
      try {
        engine.applyLaunchPlan(rns::PreparedLaunchPlan{});
        std::cerr << "empty plan was accepted\n";
        return 1;
      } catch (const std::logic_error&) {
      }
      if (engine.state() != rns::EngineState::Draft) {
        std::cerr << "empty plan mutated Draft\n";
        return 1;
      }
    }

    expectThrows(
        "mutate after prepare",
        [&] {
          rns::LaunchDraft draft(config);
          auto candidates = rns::prepareExplicitAddons(draft);
          draft.addBuiltInAddon("compat-rn73");
          (void)rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
        },
        "changed after prepareExplicitAddons");

    {
      auto counts = std::make_shared<HookCounts>();
      rns::LaunchDraft draft(config);
      draft.addAddon(
          std::make_unique<RecordingAddon>("planned-dtor", counts),
          "planned-dtor",
          rns::AddonRequestOrigin::Test);
      draft.addBundle(rns::test::memoryBundle(
          "RN$SimulatorWorkload.ready();\nRN$SimulatorWorkload.complete();\n",
          "memory://planned-dtor.js"));
      auto candidates = rns::prepareExplicitAddons(draft);
      auto plan = rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
      {
        rns::Engine engine;
        engine.applyLaunchPlan(std::move(plan));
        if (engine.state() != rns::EngineState::Planned) {
          std::cerr << "~Engine Planned setup was not Planned\n";
          return 1;
        }
      }
      if (counts->destroyed != 1 || counts->bind != 0 || counts->unbind != 0) {
        std::cerr << "~Engine Planned did not destroy unbound addons\n";
        return 1;
      }
    }

    {
      auto counts = std::make_shared<HookCounts>();
      rns::LaunchDraft draft(config);
      draft.addAddon(
          std::make_unique<RecordingAddon>("no-bundle", counts),
          "no-bundle",
          rns::AddonRequestOrigin::Test);
      auto candidates = rns::prepareExplicitAddons(draft);
      auto plan = rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
      rns::Engine engine;
      engine.applyLaunchPlan(std::move(plan));
      const auto result = engine.run();
      if (result.exitCode == 0 ||
          result.error.find("at least one bundle is required") ==
              std::string::npos ||
          counts->destroyed != 1 || counts->bind != 0) {
        std::cerr << "run() without bundles leaked addons: " << result.error
                  << '\n';
        return 1;
      }
      const auto again = engine.run();
      if (again.exitCode == 0 ||
          again.error.find("Planned") == std::string::npos) {
        std::cerr << "run() in Finished was accepted: " << again.error << '\n';
        return 1;
      }
    }

    {
      rns::LaunchDraft draft(config);
      draft.addBundle(rns::test::memoryBundle(
          "RN$SimulatorWorkload.ready();\nRN$SimulatorWorkload.complete();\n",
          "memory://second-plan.js"));
      auto candidates = rns::prepareExplicitAddons(draft);
      auto plan = rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
      rns::Engine engine;
      engine.applyLaunchPlan(std::move(plan));
      rns::LaunchDraft extra(config);
      extra.addBundle(rns::test::memoryBundle(
          "RN$SimulatorWorkload.ready();\nRN$SimulatorWorkload.complete();\n",
          "memory://second-plan-2.js"));
      auto extraCandidates = rns::prepareExplicitAddons(extra);
      auto extraPlan =
          rns::finalizeLaunchPlan(std::move(extra), std::move(extraCandidates));
      try {
        engine.applyLaunchPlan(std::move(extraPlan));
        std::cerr << "applyLaunchPlan in Planned was accepted\n";
        return 1;
      } catch (const std::logic_error&) {
      }
      if (engine.state() != rns::EngineState::Planned) {
        std::cerr << "applyLaunchPlan in Planned mutated state\n";
        return 1;
      }
    }

    std::cout << "addon-planner-smoke ok\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
