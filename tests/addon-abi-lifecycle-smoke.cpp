#include <react-native-simulator/Engine.h>
#include <react-native-simulator/SimulatorAddon.h>

#include "EngineTestSupport.h"

#include <ReactCommon/TurboModule.h>
#include <react/renderer/components/unimplementedview/UnimplementedViewComponentDescriptor.h>
#include <react/renderer/core/ComponentDescriptor.h>

#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
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

class OverlayWrapAddon final : public CountingAddon {
 public:
  using CountingAddon::CountingAddon;
  rns::AddonManifest manifest() const override {
    auto manifest = CountingAddon::manifest();
    manifest.moduleOverlays = {{"PlatformConstants", "cache wrap"}};
    return manifest;
  }
};

class ThrowingModuleAddon final : public CountingAddon {
 public:
  using CountingAddon::CountingAddon;
  rns::AddonManifest manifest() const override {
    auto manifest = CountingAddon::manifest();
    manifest.modules = {{
        "BoomModule",
        rns::RuntimeCapabilityClass::HostAdapted,
        "lookup throw",
    }};
    return manifest;
  }
  std::shared_ptr<facebook::react::TurboModule> getTurboModule(
      const rns::AddonGenerationContext&,
      facebook::jsi::Runtime&,
      const std::string&,
      const std::shared_ptr<facebook::react::CallInvoker>&) override {
    throw std::runtime_error("lookup boom");
  }
};

class NullModuleAddon final : public CountingAddon {
 public:
  using CountingAddon::CountingAddon;
  rns::AddonManifest manifest() const override {
    auto manifest = CountingAddon::manifest();
    manifest.modules = {{
        "NullModule",
        rns::RuntimeCapabilityClass::HostAdapted,
        "lookup null",
    }};
    return manifest;
  }
};

class HeldTurboModule final : public facebook::react::TurboModule {
 public:
  explicit HeldTurboModule(std::shared_ptr<std::atomic<int>> pings)
      : TurboModule("HeldModule", nullptr), pings_(std::move(pings)) {
    methodMap_["ping"] = {0, &invokePing};
  }
  void ping() {
    pings_->fetch_add(1);
  }

 private:
  static facebook::jsi::Value invokePing(
      facebook::jsi::Runtime&,
      facebook::react::TurboModule& module,
      const facebook::jsi::Value*,
      size_t) {
    static_cast<HeldTurboModule&>(module).ping();
    return true;
  }
  std::shared_ptr<std::atomic<int>> pings_;
};

class HeldModuleAddon final : public CountingAddon {
 public:
  HeldModuleAddon(
      std::string name,
      std::shared_ptr<HookCounts> counts,
      std::shared_ptr<HeldTurboModule> module)
      : CountingAddon(std::move(name), std::move(counts)),
        module_(std::move(module)) {}
  rns::AddonManifest manifest() const override {
    auto manifest = CountingAddon::manifest();
    manifest.modules = {{
        "HeldModule",
        rns::RuntimeCapabilityClass::HostAdapted,
        "quiesce-safe",
    }};
    return manifest;
  }
  std::shared_ptr<facebook::react::TurboModule> getTurboModule(
      const rns::AddonGenerationContext&,
      facebook::jsi::Runtime&,
      const std::string&,
      const std::shared_ptr<facebook::react::CallInvoker>&) override {
    return module_;
  }

 private:
  std::shared_ptr<HeldTurboModule> module_;
};

facebook::react::ComponentDescriptor::Unique throwingDescriptorCtor(
    const facebook::react::ComponentDescriptorParameters&) {
  throw std::runtime_error("constructor boom");
}

class ThrowingProviderAddon final : public CountingAddon {
 public:
  using CountingAddon::CountingAddon;
  rns::AddonManifest manifest() const override {
    auto manifest = CountingAddon::manifest();
    manifest.components = {{
        "RNSThrowView",
        rns::RuntimeCapabilityClass::HostAdapted,
        rns::AddonComponentKind::FabricDescriptor,
        {},
        {},
        "throwing ctor",
    }};
    return manifest;
  }
  void configureFabric(
      const rns::AddonGenerationContext& context,
      rns::AddonFabricRegistrar& registrar) override {
    CountingAddon::configureFabric(context, registrar);
    registrar.registerDescriptor({
        0xABCDEF01ull,
        "RNSThrowView",
        nullptr,
        &throwingDescriptorCtor,
    });
  }
};

class MismatchProviderAddon final : public CountingAddon {
 public:
  using CountingAddon::CountingAddon;
  rns::AddonManifest manifest() const override {
    auto manifest = CountingAddon::manifest();
    manifest.components = {{
        "RNSMismatchView",
        rns::RuntimeCapabilityClass::HostAdapted,
        rns::AddonComponentKind::FabricDescriptor,
        {},
        {},
        "name mismatch",
    }};
    return manifest;
  }
  void configureFabric(
      const rns::AddonGenerationContext& context,
      rns::AddonFabricRegistrar& registrar) override {
    CountingAddon::configureFabric(context, registrar);
    auto flavor = std::make_shared<std::string>("ConstructedName");
    registrar.registerDescriptor({
        reinterpret_cast<facebook::react::ComponentHandle>(flavor->c_str()),
        "RNSMismatchView",
        flavor,
        &facebook::react::concreteComponentDescriptorConstructor<
            facebook::react::UnimplementedViewComponentDescriptor>,
    });
  }
};

class DuplicateHandleAddon final : public CountingAddon {
 public:
  using CountingAddon::CountingAddon;
  rns::AddonManifest manifest() const override {
    auto manifest = CountingAddon::manifest();
    manifest.components = {
        {
            "RNSDupA",
            rns::RuntimeCapabilityClass::HostAdapted,
            rns::AddonComponentKind::FabricDescriptor,
            {},
            {},
            "dup-a",
        },
        {
            "RNSDupB",
            rns::RuntimeCapabilityClass::HostAdapted,
            rns::AddonComponentKind::FabricDescriptor,
            {},
            {},
            "dup-b",
        },
    };
    return manifest;
  }
  void configureFabric(
      const rns::AddonGenerationContext& context,
      rns::AddonFabricRegistrar& registrar) override {
    CountingAddon::configureFabric(context, registrar);
    registrar.registerDescriptor({
        0x11111111ull,
        "RNSDupA",
        nullptr,
        &throwingDescriptorCtor,
    });
    registrar.registerDescriptor({
        0x11111111ull,
        "RNSDupB",
        nullptr,
        &throwingDescriptorCtor,
    });
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

rns::EngineResult runWithScript(
    rns::EngineConfig config,
    std::unique_ptr<rns::SimulatorAddon> addon,
    std::string label,
    std::string script) {
  rns::LaunchDraft draft(std::move(config));
  draft.addAddon(std::move(addon), std::move(label), rns::AddonRequestOrigin::Test);
  draft.addBundle(rns::test::memoryBundle(std::move(script), "memory://abi.js"));
  auto candidates = rns::prepareExplicitAddons(draft);
  auto plan = rns::finalizeLaunchPlan(std::move(draft), std::move(candidates));
  rns::Engine engine;
  engine.applyLaunchPlan(std::move(plan));
  return engine.run();
}

rns::EngineResult runWith(
    rns::EngineConfig config,
    std::unique_ptr<rns::SimulatorAddon> addon,
    std::string label) {
  return runWithScript(
      std::move(config),
      std::move(addon),
      std::move(label),
      "RN$SimulatorWorkload.ready();\n"
      "globalThis.RN$SimulatorWorkloadResult={iterations:1,checksum:1};\n"
      "RN$SimulatorWorkload.complete();\n");
}

int listenInspectorSocket(const std::filesystem::path& path) {
  const int server = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (server < 0) {
    throw std::runtime_error("inspector socket create failed");
  }
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::strncpy(
      address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
  ::unlink(path.c_str());
  if (::bind(
          server,
          reinterpret_cast<sockaddr*>(&address),
          sizeof(address)) != 0 ||
      ::listen(server, 1) != 0) {
    ::close(server);
    throw std::runtime_error("inspector socket bind/listen failed");
  }
  return server;
}

std::string readInspectorSnapshots(int server) {
  const int client = ::accept(server, nullptr, nullptr);
  ::close(server);
  if (client < 0) {
    throw std::runtime_error("inspector accept failed");
  }
  std::string buffer;
  char chunk[4096];
  while (buffer.find("\"type\":\"snapshot\"") == std::string::npos) {
    const auto read = ::recv(client, chunk, sizeof(chunk), 0);
    if (read <= 0) {
      break;
    }
    buffer.append(chunk, static_cast<size_t>(read));
  }
  ::close(client);
  std::string last;
  size_t cursor = 0;
  while (cursor < buffer.size()) {
    const auto newline = buffer.find('\n', cursor);
    const auto line = buffer.substr(
        cursor,
        newline == std::string::npos ? std::string::npos : newline - cursor);
    if (line.find("\"type\":\"snapshot\"") != std::string::npos) {
      last = line;
    }
    if (newline == std::string::npos) {
      break;
    }
    cursor = newline + 1;
  }
  return last;
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

    {
      auto counts = std::make_shared<HookCounts>();
      const auto result = runWith(
          config,
          std::make_unique<ThrowingProviderAddon>("throw-provider", counts),
          "throw-provider");
      if (result.exitCode == 0 ||
          result.error.find("constructor threw") == std::string::npos) {
        std::cerr << "throwing Fabric constructor was not rejected: "
                  << result.error << '\n';
        return 1;
      }
    }

    {
      auto counts = std::make_shared<HookCounts>();
      const auto result = runWith(
          config,
          std::make_unique<MismatchProviderAddon>("mismatch-provider", counts),
          "mismatch-provider");
      if (result.exitCode == 0 ||
          result.error.find("name mismatch") == std::string::npos) {
        std::cerr << "Fabric name mismatch was not rejected: " << result.error
                  << '\n';
        return 1;
      }
    }

    {
      auto counts = std::make_shared<HookCounts>();
      try {
        const auto result = runWith(
            config,
            std::make_unique<DuplicateHandleAddon>("dup-handle", counts),
            "dup-handle");
        if (result.exitCode == 0) {
          std::cerr << "duplicate component handle was accepted\n";
          return 1;
        }
      } catch (const std::exception& error) {
        if (std::string(error.what()).find("duplicate component handle") ==
            std::string::npos) {
          std::cerr << "duplicate handle error missing handle: " << error.what()
                    << '\n';
          return 1;
        }
      }
    }

    {
      auto counts = std::make_shared<HookCounts>();
      const auto result = runWithScript(
          config,
          std::make_unique<OverlayWrapAddon>("overlay-wrap", counts),
          "overlay-wrap",
          "globalThis.nativeModuleProxy.PlatformConstants;\n"
          "globalThis.nativeModuleProxy.PlatformConstants;\n"
          "const has = globalThis.__nativeComponentRegistry__hasComponent;\n"
          "if (!has('RootView') || !has('View') || !has('RCTView') ||\n"
          "    has('NotARealView') || has('RCTNotARealView')) {\n"
          "  throw new Error('hasComponent ledger mismatch');\n"
          "}\n"
          "RN$SimulatorWorkload.ready();\n"
          "globalThis.RN$SimulatorWorkloadResult={iterations:1,checksum:1};\n"
          "RN$SimulatorWorkload.complete();\n");
      if (result.exitCode != 0 || counts->wrap != 1) {
        std::cerr << "wrapTurboModule cache skip failed wrap=" << counts->wrap
                  << " " << result.error << '\n';
        return 1;
      }
    }

    {
      auto counts = std::make_shared<HookCounts>();
      const auto result = runWithScript(
          config,
          std::make_unique<ThrowingModuleAddon>("throw-module", counts),
          "throw-module",
          "globalThis.nativeModuleProxy.BoomModule;\n"
          "RN$SimulatorWorkload.ready();\n"
          "globalThis.RN$SimulatorWorkloadResult={iterations:1,checksum:1};\n"
          "RN$SimulatorWorkload.complete();\n");
      if (result.exitCode == 0) {
        std::cerr << "lookup-time throw escaped as a successful run\n";
        return 1;
      }
    }

    {
      auto counts = std::make_shared<HookCounts>();
      const auto result = runWithScript(
          config,
          std::make_unique<NullModuleAddon>("null-module", counts),
          "null-module",
          "globalThis.nativeModuleProxy.NullModule;\n"
          "RN$SimulatorWorkload.ready();\n"
          "globalThis.RN$SimulatorWorkloadResult={iterations:1,checksum:1};\n"
          "RN$SimulatorWorkload.complete();\n");
      if (result.exitCode == 0) {
        std::cerr << "lookup-time null escaped as a successful run\n";
        return 1;
      }
    }

    {
      auto counts = std::make_shared<HookCounts>();
      auto pings = std::make_shared<std::atomic<int>>(0);
      auto module = std::make_shared<HeldTurboModule>(pings);
      const auto result = runWithScript(
          config,
          std::make_unique<HeldModuleAddon>("held-module", counts, module),
          "held-module",
          "globalThis.nativeModuleProxy.HeldModule.ping();\n"
          "RN$SimulatorWorkload.ready();\n"
          "globalThis.RN$SimulatorWorkloadResult={iterations:1,checksum:1};\n"
          "RN$SimulatorWorkload.complete();\n");
      if (result.exitCode != 0 || pings->load() < 1) {
        std::cerr << "held TurboModule was not called from JS: " << result.error
                  << '\n';
        return 1;
      }
      module->ping();
      if (pings->load() < 2) {
        std::cerr << "held TurboModule was unsafe after quiesce\n";
        return 1;
      }
    }

    {
      auto counts = std::make_shared<HookCounts>();
      const auto socketPath = std::filesystem::temp_directory_path() /
          ("rns-inspector-" + std::to_string(::getpid()) + ".sock");
      const int server = listenInspectorSocket(socketPath);
      std::string live;
      std::exception_ptr listenerError;
      std::thread listener([&] {
        try {
          live = readInspectorSnapshots(server);
        } catch (...) {
          listenerError = std::current_exception();
        }
      });
      rns::EngineConfig inspected = config;
      inspected.inspectorSocket = socketPath;
      const auto result = runWith(
          inspected,
          std::make_unique<CountingAddon>("live-serial", counts),
          "live-serial");
      listener.join();
      std::filesystem::remove(socketPath);
      if (listenerError) {
        std::rethrow_exception(listenerError);
      }
      const auto liveHas =
          live.find("\"name\":\"live-serial\"") != std::string::npos &&
          live.find("\"name\":\"safe-area\"") != std::string::npos &&
          live.find("\"name\":\"RootView\"") != std::string::npos &&
          live.find("\"schemaVersion\":3") != std::string::npos &&
          live.find("\"addonAbi\":4") != std::string::npos &&
          live.find("\"jsVisibleReactNativeVersion\":\"0.87.0\"") !=
              std::string::npos;
      const auto finalHas =
          result.metricsJson.find("\"name\":\"live-serial\"") !=
              std::string::npos &&
          result.metricsJson.find("\"name\":\"safe-area\"") !=
              std::string::npos &&
          result.metricsJson.find("\"name\":\"RootView\"") !=
              std::string::npos &&
          result.metricsJson.find("\"schemaVersion\":3") != std::string::npos &&
          result.metricsJson.find("\"addonAbi\":4") != std::string::npos &&
          result.metricsJson.find("\"jsVisibleReactNativeVersion\":\"0.87.0\"") !=
              std::string::npos;
      if (result.exitCode != 0 || !liveHas || !finalHas) {
        std::cerr << "live and final serializers drifted\n"
                  << live << '\n' << result.metricsJson << '\n';
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
