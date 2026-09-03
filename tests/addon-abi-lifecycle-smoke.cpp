#include <react-native-simulator/Engine.h>
#include <react-native-simulator/SimulatorAddon.h>

#include "EngineTestSupport.h"

#include <ReactCommon/TurboModule.h>

#include <dlfcn.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

template <class T>
concept HasEngineLoadBundle = requires(T& engine, const std::string& path) {
  engine.loadBundle(path);
};
template <class T>
concept HasEngineAddAddon = requires(T& engine, std::string name) {
  engine.addAddon(name);
};
static_assert(!HasEngineLoadBundle<ReactNativeSimulator::Engine>);
static_assert(!HasEngineAddAddon<ReactNativeSimulator::Engine>);

namespace rns = ReactNativeSimulator;

namespace {
struct HookCounts {
  int bind{0};
  int unbind{0};
  int configure{0};
  int install{0};
  int quiesce{0};
  int wrap{0};
};

class CountingAddon : public rns::SimulatorAddon {
 public:
  CountingAddon(std::string name, std::shared_ptr<HookCounts> counts)
      : name_(std::move(name)), counts_(std::move(counts)) {}

  rns::AddonManifest manifest() const override {
    rns::AddonManifest manifest;
    manifest.name = name_;
    manifest.addonVersion = "0.0.1";
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
    ++counts_->wrap;
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

 protected:
  std::string name_;
  std::shared_ptr<HookCounts> counts_;
};

class ThrowingBindAddon final : public CountingAddon {
 public:
  using CountingAddon::CountingAddon;
  void bind(const rns::AddonHost& host) override {
    CountingAddon::bind(host);
    throw std::runtime_error("bind failed");
  }
};

class DummyTurboModule final : public facebook::react::TurboModule {
 public:
  DummyTurboModule() : TurboModule("Dummy", nullptr) {}
};

class IdentityWrapAddon final : public CountingAddon {
 public:
  using CountingAddon::CountingAddon;
  void installJSI(
      const rns::AddonGenerationContext& context,
      facebook::jsi::Runtime& runtime,
      const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker) override {
    CountingAddon::installJSI(context, runtime, jsInvoker);
    auto framework = std::make_shared<DummyTurboModule>();
    auto wrapped = wrapTurboModule(
        context, runtime, "Dummy", framework, jsInvoker);
    if (wrapped.get() != framework.get()) {
      throw std::runtime_error("wrapTurboModule identity failed");
    }
  }
};

class MutatingJsiAddon final : public CountingAddon {
 public:
  using CountingAddon::CountingAddon;
  void installJSI(
      const rns::AddonGenerationContext& context,
      facebook::jsi::Runtime& runtime,
      const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker) override {
    CountingAddon::installJSI(context, runtime, jsInvoker);
    runtime.global().setProperty(runtime, "RN$Simulator", 1);
  }
};

class ExecutorHoldAddon final : public CountingAddon {
 public:
  ExecutorHoldAddon(
      std::string name,
      std::shared_ptr<HookCounts> counts,
      std::shared_ptr<rns::AddonRuntimeExecutor> hold)
      : CountingAddon(std::move(name), std::move(counts)), hold_(std::move(hold)) {}
  void installJSI(
      const rns::AddonGenerationContext& context,
      facebook::jsi::Runtime& runtime,
      const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker) override {
    CountingAddon::installJSI(context, runtime, jsInvoker);
    *hold_ = context.executor;
  }

 private:
  std::shared_ptr<rns::AddonRuntimeExecutor> hold_;
};

class CollisionComponentAddon final : public CountingAddon {
 public:
  using CountingAddon::CountingAddon;
  rns::AddonManifest manifest() const override {
    auto manifest = CountingAddon::manifest();
    manifest.components = {{
        "View",
        rns::RuntimeCapabilityClass::HostAdapted,
        rns::AddonComponentKind::FabricDescriptor,
        {},
        {},
        "must-collide",
    }};
    return manifest;
  }
};

class ExtraProviderAddon final : public CountingAddon {
 public:
  using CountingAddon::CountingAddon;
  void configureFabric(
      const rns::AddonGenerationContext& context,
      rns::AddonFabricRegistrar& registrar) override {
    CountingAddon::configureFabric(context, registrar);
    registrar.registerDescriptor({});
  }
};

class ThrowingConfigureAddon final : public CountingAddon {
 public:
  using CountingAddon::CountingAddon;
  void configureFabric(
      const rns::AddonGenerationContext& context,
      rns::AddonFabricRegistrar& registrar) override {
    CountingAddon::configureFabric(context, registrar);
    throw std::runtime_error("configure failed");
  }
};

rns::EngineResult runWith(
    rns::EngineConfig config,
    std::unique_ptr<rns::SimulatorAddon> addon,
    std::string label) {
  rns::LaunchDraft draft(std::move(config));
  draft.addAddon(std::move(addon), std::move(label), rns::AddonRequestOrigin::Test);
  draft.addBundle(rns::test::memoryBundle(
      "RN$SimulatorWorkload.ready();\n"
      "globalThis.RN$SimulatorWorkloadResult={iterations:1,checksum:1};\n"
      "RN$SimulatorWorkload.complete();\n",
      "memory://abi.js"));
  auto candidates = rns::prepareExplicitAddons(draft);
  auto plan = rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
  rns::Engine engine;
  engine.applyLaunchPlan(std::move(plan));
  return engine.run();
}
} // namespace

int main(int argc, char** argv) {
  try {
    rns::EngineConfig config;
    config.iterations = 1;
    config.timeoutMs = 1000;
    config.profile = "android-rn87";

    {
      auto counts = std::make_shared<HookCounts>();
      const auto result = runWith(
          config,
          std::make_unique<CountingAddon>("counting", counts),
          "counting");
      if (result.exitCode != 0 || counts->bind != 1 || counts->unbind != 1 ||
          counts->configure != 1 || counts->install != 1 ||
          counts->quiesce != 1 || counts->wrap != 0) {
        std::cerr << "successful ABI hook counts failed\n";
        return 1;
      }
    }

    {
      auto first = std::make_shared<HookCounts>();
      auto second = std::make_shared<HookCounts>();
      rns::LaunchDraft draft(config);
      draft.addAddon(
          std::make_unique<CountingAddon>("counting", first),
          "counting",
          rns::AddonRequestOrigin::Test);
      draft.addAddon(
          std::make_unique<ThrowingBindAddon>("throw-bind", second),
          "throw-bind",
          rns::AddonRequestOrigin::Test);
      draft.addBundle(rns::test::memoryBundle(
          "RN$SimulatorWorkload.ready();\nRN$SimulatorWorkload.complete();\n",
          "memory://abi-bind.js"));
      auto candidates = rns::prepareExplicitAddons(draft);
      auto plan = rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
      rns::Engine engine;
      engine.applyLaunchPlan(std::move(plan));
      const auto result = engine.run();
      if (result.exitCode == 0 || second->unbind != 1 || first->unbind != 1 ||
          first->configure != 0) {
        std::cerr << "throwing bind did not unbind entered addons: "
                  << result.error << '\n';
        return 1;
      }
    }

    {
      auto ok = std::make_shared<HookCounts>();
      auto boom = std::make_shared<HookCounts>();
      rns::LaunchDraft draft(config);
      draft.addAddon(
          std::make_unique<CountingAddon>("counting", ok),
          "counting",
          rns::AddonRequestOrigin::Test);
      draft.addAddon(
          std::make_unique<ThrowingConfigureAddon>("throw-configure", boom),
          "throw-configure",
          rns::AddonRequestOrigin::Test);
      draft.addBundle(rns::test::memoryBundle(
          "RN$SimulatorWorkload.ready();\nRN$SimulatorWorkload.complete();\n",
          "memory://abi-configure.js"));
      auto candidates = rns::prepareExplicitAddons(draft);
      auto plan = rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
      rns::Engine engine;
      engine.applyLaunchPlan(std::move(plan));
      const auto result = engine.run();
      if (result.exitCode == 0 || ok->configure != 1 || boom->configure != 1 ||
          ok->install != 0 || boom->install != 0 || ok->quiesce != 1 ||
          boom->quiesce != 1) {
        std::cerr << "failed generation prefix/quiesce contract failed: "
                  << result.error << '\n';
        return 1;
      }
    }

    {
      auto counts = std::make_shared<HookCounts>();
      const auto result = runWith(
          config,
          std::make_unique<IdentityWrapAddon>("identity-wrap", counts),
          "identity-wrap");
      if (result.exitCode != 0 || counts->wrap != 1) {
        std::cerr << "wrapTurboModule identity self-check failed\n"
                  << result.error << '\n';
        return 1;
      }
    }

    {
      auto counts = std::make_shared<HookCounts>();
      const auto result = runWith(
          config,
          std::make_unique<MutatingJsiAddon>("mutate-jsi", counts),
          "mutate-jsi");
      if (result.exitCode == 0 ||
          result.error.find("protected global mutated") == std::string::npos) {
        std::cerr << "protected-global mutation was not rejected: "
                  << result.error << '\n';
        return 1;
      }
    }

    {
      auto counts = std::make_shared<HookCounts>();
      auto hold = std::make_shared<rns::AddonRuntimeExecutor>();
      const auto result = runWith(
          config,
          std::make_unique<ExecutorHoldAddon>("exec-hold", counts, hold),
          "exec-hold");
      if (result.exitCode != 0) {
        std::cerr << "executor hold run failed: " << result.error << '\n';
        return 1;
      }
      const bool posted = hold->post([](facebook::jsi::Runtime&) {});
      if (posted) {
        std::cerr << "executor post after quiesce was delivered\n";
        return 1;
      }
    }

    try {
      rns::LaunchDraft draft(config);
      draft.addAddon(
          std::make_unique<CollisionComponentAddon>(
              "collide-view", std::make_shared<HookCounts>()),
          "collide-view",
          rns::AddonRequestOrigin::Test);
      auto candidates = rns::prepareExplicitAddons(draft);
      (void)rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
      std::cerr << "framework component collision was accepted\n";
      return 1;
    } catch (const rns::TerminalLaunchPlanError& error) {
      if (std::string(error.what()).find("View") == std::string::npos) {
        std::cerr << "component collision error missing View: " << error.what()
                  << '\n';
        return 1;
      }
    }

    {
      auto counts = std::make_shared<HookCounts>();
      const auto result = runWith(
          config,
          std::make_unique<ExtraProviderAddon>("extra-provider", counts),
          "extra-provider");
      if (result.exitCode == 0) {
        std::cerr << "extra/unowned descriptor registration was accepted\n";
        return 1;
      }
    }

    if (argc > 1) {
      const std::filesystem::path modulePath = argv[1];
      rns::LaunchDraft draft(config);
      draft.addAddonPath(modulePath, rns::AddonRequestOrigin::Test);
      auto candidates = rns::prepareExplicitAddons(draft);
      void* handle = dlopen(modulePath.c_str(), RTLD_NOW | RTLD_NOLOAD);
      Dl_info info{};
      auto* symbol = handle == nullptr
          ? nullptr
          : dlsym(handle, rns::kSimulatorAddonEntryPoint);
      const bool provenance = symbol != nullptr && dladdr(symbol, &info) != 0 &&
          info.dli_fname != nullptr &&
          std::string(info.dli_fname).find("rns-addon-fabric-probe") !=
              std::string::npos;
      if (handle != nullptr) {
        dlclose(handle);
      }
      if (!provenance) {
        std::cerr << "dladdr provenance failed while MODULE was mapped\n";
        return 1;
      }
      draft.addBundle(rns::test::memoryBundle(
          "const has = globalThis.__nativeComponentRegistry__hasComponent"
          "('RNSFabricProbeView');\n"
          "if (!has) throw new Error('fabric probe component missing');\n"
          "RN$SimulatorWorkload.ready();\n"
          "globalThis.RN$SimulatorWorkloadResult={iterations:1,checksum:1};\n"
          "RN$SimulatorWorkload.complete();\n",
          "memory://abi-module.js"));
      auto plan = rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
      rns::Engine engine;
      engine.applyLaunchPlan(std::move(plan));
      const auto result = engine.run();
      if (result.exitCode != 0 ||
          result.metricsJson.find("\"name\":\"fabric-probe\"") ==
              std::string::npos ||
          result.metricsJson.find("\"source\":\"module\"") ==
              std::string::npos ||
          result.metricsJson.find("\"name\":\"RNSFabricProbeView\"") ==
              std::string::npos) {
        std::cerr << "MODULE load failed\n" << result.error << '\n'
                  << result.metricsJson << '\n';
        return 1;
      }
    }

    std::cout << "addon-abi-lifecycle-smoke ok\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
