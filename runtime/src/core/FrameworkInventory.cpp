#include "FrameworkInventory.h"

#include <react-native-simulator/Engine.h>

#include "HeadlessOfficialComponents.h"
#include "HeadlessSampleComponent.h"
#include "HeadlessTextInputComponent.h"
#include "HeadlessTurboModules.h"

#include <react/renderer/components/image/ImageComponentDescriptor.h>
#include <react/renderer/components/root/RootComponentDescriptor.h>
#include <react/renderer/components/scrollview/AndroidHorizontalScrollContentViewComponentDescriptor.h>
#include <react/renderer/components/scrollview/ScrollViewComponentDescriptor.h>
#include <react/renderer/components/text/ParagraphComponentDescriptor.h>
#include <react/renderer/components/text/RawTextComponentDescriptor.h>
#include <react/renderer/components/text/TextComponentDescriptor.h>
#include <react/renderer/components/unimplementedview/UnimplementedViewComponentDescriptor.h>
#include <react/renderer/components/view/LayoutConformanceComponentDescriptor.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>
#include <react/renderer/components/root/RootShadowNode.h>

#include <stdexcept>
#include <utility>

namespace react = facebook::react;
namespace rns = ReactNativeSimulator;

namespace {

using rns::AddonComponentKind;
using rns::FrameworkComponentEntry;
using rns::FrameworkModuleEntry;
using rns::InventoryRuntimeBindings;
using rns::RuntimeCapabilityClass;

FrameworkModuleEntry hostModule(
    std::string name,
    RuntimeCapabilityClass classification,
    std::string note,
    std::shared_ptr<InventoryRuntimeBindings> bindings) {
  FrameworkModuleEntry entry;
  entry.contract = {
      .name = name,
      .classification = classification,
      .owner = "host",
      .note = std::move(note),
  };
  entry.factory = [bindings = std::move(bindings), name = std::move(name)](
                      facebook::jsi::Runtime& runtime,
                      const std::shared_ptr<facebook::react::CallInvoker>&
                          jsInvoker) {
    return getHeadlessHostTurboModule(
        runtime, name, jsInvoker, bindings ? bindings->eventLoop : nullptr);
  };
  return entry;
}

FrameworkModuleEntry profileModule(
    std::string name,
    RuntimeCapabilityClass classification,
    std::string owner,
    std::string note,
    std::shared_ptr<InventoryRuntimeBindings> bindings) {
  FrameworkModuleEntry entry;
  entry.contract = {
      .name = name,
      .classification = classification,
      .owner = owner,
      .note = std::move(note),
  };
  entry.factory = [bindings = std::move(bindings),
                   name = std::move(name),
                   owner](
                      facebook::jsi::Runtime& runtime,
                      const std::shared_ptr<facebook::react::CallInvoker>&
                          jsInvoker) {
    HeadlessRNModuleHost host{};
    if (bindings) {
      host = bindings->moduleHost;
    }
    return getHeadlessRNModule(runtime, name, owner, jsInvoker, host);
  };
  return entry;
}

template <typename Descriptor>
FrameworkComponentEntry realComponent(
    std::string name,
    RuntimeCapabilityClass classification,
    std::string owner,
    std::string note) {
  FrameworkComponentEntry entry;
  entry.contract = {
      .name = name,
      .classification = classification,
      .owner = std::move(owner),
      .kind = AddonComponentKind::FabricDescriptor,
      .note = std::move(note),
  };
  entry.provider = react::concreteComponentDescriptorProvider<Descriptor>();
  return entry;
}

FrameworkComponentEntry unimplementedComponent(
    std::string name,
    RuntimeCapabilityClass classification,
    std::string owner,
    std::string note) {
  FrameworkComponentEntry entry;
  entry.flavor = std::make_shared<std::string>(name);
  entry.contract = {
      .name = std::move(name),
      .classification = classification,
      .owner = std::move(owner),
      .kind = AddonComponentKind::DescriptorOnlyMock,
      .note = std::move(note),
  };
  entry.provider = {
      reinterpret_cast<react::ComponentHandle>(entry.flavor->c_str()),
      entry.flavor->c_str(),
      entry.flavor,
      &react::concreteComponentDescriptorConstructor<
          react::UnimplementedViewComponentDescriptor>};
  return entry;
}

std::string imageNote() {
#if RNS_ENABLE_SKIA
  return "skia-local-and-http-image";
#else
  return "unavailable-without-skia";
#endif
}

RuntimeCapabilityClass imageClass() {
#if RNS_ENABLE_SKIA
  return RuntimeCapabilityClass::Implemented;
#else
  return RuntimeCapabilityClass::Unavailable;
#endif
}

std::string paragraphNote() {
#if RNS_ENABLE_SKIA
  return "skia-prepared-text";
#else
  return "unavailable-without-skia";
#endif
}

RuntimeCapabilityClass paragraphClass() {
#if RNS_ENABLE_SKIA
  return RuntimeCapabilityClass::Implemented;
#else
  return RuntimeCapabilityClass::Unavailable;
#endif
}

} // namespace

namespace ReactNativeSimulator {

bool isKnownProfileName(const std::string& name) {
  return name == "macos-rn87" || name == "android-rn87" || name == "ios-rn87";
}

RuntimeProfileDescriptor profileDescriptorFor(const std::string& name) {
  if (name == "macos-rn87") {
    return {.name = name, .platform = "macos", .compatibilityLevel = "native-headless"};
  }
  if (name == "android-rn87") {
    return {
        .name = name,
        .platform = "android",
        .compatibilityLevel = "native-headless-platform-adapter"};
  }
  if (name == "ios-rn87") {
    return {
        .name = name,
        .platform = "ios",
        .compatibilityLevel = "native-headless-platform-adapter"};
  }
  throw TerminalLaunchPlanError("Unknown profile: " + name);
}

FrameworkSurfaceInventory buildFrameworkSurfaceInventory(
    const std::string& profileName,
    const std::shared_ptr<InventoryRuntimeBindings>& bindings) {
  FrameworkSurfaceInventory inventory;
  inventory.profile = profileDescriptorFor(profileName);
  const auto owner = inventory.profile.name;
  auto host = [&](std::string name,
                  RuntimeCapabilityClass classification,
                  std::string note) {
    inventory.hostModules.push_back(hostModule(
        std::move(name), classification, std::move(note), bindings));
  };
  auto profile = [&](std::string name,
                     RuntimeCapabilityClass classification,
                     std::string note) {
    inventory.profileModules.push_back(profileModule(
        std::move(name), classification, owner, std::move(note), bindings));
  };

  host("NativeReactNativeFeatureFlagsCxx", RuntimeCapabilityClass::Implemented, "real-headless");
  host("NativeMicrotasksCxx", RuntimeCapabilityClass::Implemented, "real-headless");
  host("NativeDOMCxx", RuntimeCapabilityClass::Implemented, "real-headless");
  host("NativeAnimatedModule", RuntimeCapabilityClass::HostAdapted, "shared-animation-backend");
  host("NativeIdleCallbacksCxx", RuntimeCapabilityClass::Implemented, "runtime-scheduler-idle");
  host("NativePerformanceCxx", RuntimeCapabilityClass::HostAdapted, "host-performance-timeline");
  host("NativeIntersectionObserverCxx", RuntimeCapabilityClass::Implemented, "rn-intersection-observer");
  host("NativeMutationObserverCxx", RuntimeCapabilityClass::Implemented, "rn-mutation-observer");
  host("ExceptionsManager", RuntimeCapabilityClass::HostAdapted, "headless-adapter");
  host("UIManager", RuntimeCapabilityClass::HostAdapted, "fabric-layout-animation");
  host("LogBox", RuntimeCapabilityClass::HostAdapted, "headless-adapter");
  host("DevSettings", RuntimeCapabilityClass::HostAdapted, "interactive-reload");
  host("HeadlessSampleModule", RuntimeCapabilityClass::Implemented, "real-headless");
  host("DeviceInfo", RuntimeCapabilityClass::HostAdapted, "headless-adapter");
  host("SourceCode", RuntimeCapabilityClass::HostAdapted, "headless-adapter");
  host("AppState", RuntimeCapabilityClass::HostAdapted, "host-environment-events");
  host("WebSocketModule", RuntimeCapabilityClass::HostAdapted, "urlsession-websocket");
  host("BlobModule", RuntimeCapabilityClass::HostAdapted, "in-memory-blob-store");
  host("FileReaderModule", RuntimeCapabilityClass::HostAdapted, "in-memory-blob-store");
  host("SegmentFetcher", RuntimeCapabilityClass::HostAdapted, "headless-adapter");
  host("Clipboard", RuntimeCapabilityClass::HostAdapted, "nspasteboard");
  host("Vibration", RuntimeCapabilityClass::Mocked, "imgui-or-mock");
  host("Appearance", RuntimeCapabilityClass::HostAdapted, "host-environment-events");
  host("I18nManager", RuntimeCapabilityClass::HostAdapted, "headless-adapter");
  host("Networking", RuntimeCapabilityClass::HostAdapted, "urlsession-http");
  host("ImageLoader", RuntimeCapabilityClass::HostAdapted, "local-and-http-size");
  host("ImageEditingManager", RuntimeCapabilityClass::HostAdapted, "imageio-crop-and-file-store");
  host("ImageStoreManager", RuntimeCapabilityClass::HostAdapted, "imageio-crop-and-file-store");
  host("KeyboardObserver", RuntimeCapabilityClass::HostAdapted, "headless-keyboard-metrics");
  host("AccessibilityInfo", RuntimeCapabilityClass::HostAdapted, "host-environment-events");
  host("HeadlessJsTaskSupport", RuntimeCapabilityClass::Mocked, "headless-adapter");
  host("FrameRateLogger", RuntimeCapabilityClass::Mocked, "headless-adapter");
  host("ModalManager", RuntimeCapabilityClass::Mocked, "headless-adapter");
  host("DevLoadingView", RuntimeCapabilityClass::HostAdapted, "headless-adapter");
  host("RedBox", RuntimeCapabilityClass::HostAdapted, "headless-adapter");
  host("ReactDevToolsRuntimeSettingsModule", RuntimeCapabilityClass::HostAdapted, "in-memory-dev-settings");

  // DeviceInfo and friends are served by getHeadlessRNModule. Route those
  // host-owned names through the profile dispatcher with owner "host".
  for (auto& entry : inventory.hostModules) {
    const auto& name = entry.contract.name;
    if (name.rfind("Native", 0) == 0 || name == "ExceptionsManager" ||
        name == "UIManager" || name == "LogBox" || name == "DevSettings" ||
        name == "HeadlessSampleModule") {
      continue;
    }
    entry.factory = [bindings, name, profileName](
                        facebook::jsi::Runtime& runtime,
                        const std::shared_ptr<facebook::react::CallInvoker>&
                            jsInvoker) {
      HeadlessRNModuleHost host{};
      if (bindings) {
        host = bindings->moduleHost;
      }
      return getHeadlessRNModule(runtime, name, profileName, jsInvoker, host);
    };
  }

  if (inventory.profile.platform == "android") {
    profile("PlatformConstants", RuntimeCapabilityClass::HostAdapted, "rn-0.87-platform-constants");
    profile("StatusBarManager", RuntimeCapabilityClass::HostAdapted, "skia-status-bar");
    profile("ToastAndroid", RuntimeCapabilityClass::HostAdapted, "skia-toast");
    profile("IntentAndroid", RuntimeCapabilityClass::Mocked, "imgui-or-mock");
    profile("PermissionsAndroid", RuntimeCapabilityClass::Mocked, "imgui-or-mock");
    profile("DialogManagerAndroid", RuntimeCapabilityClass::Mocked, "imgui-or-mock");
    profile("ShareModule", RuntimeCapabilityClass::Mocked, "imgui-or-mock");
    profile("DeviceEventManager", RuntimeCapabilityClass::HostAdapted, "hardware-back-press");
    profile("SoundManager", RuntimeCapabilityClass::Mocked, "headless-adapter");
    profile("LinkingManager", RuntimeCapabilityClass::Mocked, "imgui-or-mock");
    profile("ReactDevToolsSettingsManager", RuntimeCapabilityClass::HostAdapted, "in-memory-dev-settings");
  } else if (inventory.profile.platform == "ios") {
    profile("PlatformConstants", RuntimeCapabilityClass::HostAdapted, "rn-0.87-platform-constants");
    profile("LinkingManager", RuntimeCapabilityClass::Mocked, "imgui-or-mock");
    profile("AlertManager", RuntimeCapabilityClass::Mocked, "imgui-or-mock");
    profile("ActionSheetManager", RuntimeCapabilityClass::Mocked, "imgui-or-mock");
    profile("SettingsManager", RuntimeCapabilityClass::HostAdapted, "in-memory-settings");
    profile("StatusBarManager", RuntimeCapabilityClass::HostAdapted, "skia-status-bar");
    profile("AccessibilityManager", RuntimeCapabilityClass::HostAdapted, "host-environment-events");
    profile("PushNotificationManager", RuntimeCapabilityClass::HostAdapted, "headless-adapter");
  }

  inventory.baseComponents.push_back(realComponent<react::RootComponentDescriptor>(
      react::RootComponentName, RuntimeCapabilityClass::Implemented, "host", "real-fabric-root"));
  inventory.baseComponents.push_back(realComponent<react::ViewComponentDescriptor>(
      "View", RuntimeCapabilityClass::Implemented, "host", "real-fabric-yoga"));
  inventory.baseComponents.push_back(realComponent<react::LayoutConformanceComponentDescriptor>(
      "LayoutConformance", RuntimeCapabilityClass::Implemented, "host", "real-fabric-yoga"));
  inventory.baseComponents.push_back(realComponent<react::RawTextComponentDescriptor>(
      "RawText", RuntimeCapabilityClass::Implemented, "host", "real-fabric-virtual-text"));
  inventory.baseComponents.push_back(realComponent<react::TextComponentDescriptor>(
      "Text", RuntimeCapabilityClass::Implemented, "host", "real-fabric-virtual-text"));
  inventory.baseComponents.push_back(realComponent<react::ParagraphComponentDescriptor>(
      "Paragraph", paragraphClass(), "host", paragraphNote()));
  inventory.baseComponents.push_back(realComponent<react::ScrollViewComponentDescriptor>(
      "ScrollView", RuntimeCapabilityClass::HostAdapted, "host", "headless-viewport-state"));
  inventory.baseComponents.push_back(realComponent<react::ImageComponentDescriptor>(
      "Image", imageClass(), "host", imageNote()));
  inventory.baseComponents.push_back(
      realComponent<react::HeadlessSampleViewComponentDescriptor>(
          "HeadlessSampleView",
          RuntimeCapabilityClass::Implemented,
          "host",
          "real-headless"));

  inventory.officialComponents.push_back(
      realComponent<react::HeadlessActivityIndicatorComponentDescriptor>(
          "ActivityIndicatorView",
          RuntimeCapabilityClass::Implemented,
          owner,
          "skia-activity-indicator"));
  inventory.officialComponents.push_back(
      realComponent<react::HeadlessAndroidSwitchComponentDescriptor>(
          "AndroidSwitch", RuntimeCapabilityClass::Implemented, owner, "skia-switch"));
  inventory.officialComponents.push_back(
      realComponent<react::HeadlessSwitchComponentDescriptor>(
          "Switch", RuntimeCapabilityClass::LayoutOnly, owner, "layout-only-placeholder"));
  inventory.officialComponents.push_back(
      realComponent<react::HeadlessAndroidProgressBarComponentDescriptor>(
          "AndroidProgressBar",
          RuntimeCapabilityClass::Implemented,
          owner,
          "skia-progress-indicator"));
  inventory.officialComponents.push_back(
      realComponent<react::HeadlessModalHostViewComponentDescriptor>(
          "ModalHostView", RuntimeCapabilityClass::Implemented, owner, "skia-modal-host"));
  inventory.officialComponents.push_back(
      realComponent<react::HeadlessAndroidDrawerLayoutComponentDescriptor>(
          "AndroidDrawerLayout",
          RuntimeCapabilityClass::Implemented,
          owner,
          "skia-drawer-layout"));
  inventory.officialComponents.push_back(
      realComponent<react::HeadlessAndroidSwipeRefreshLayoutComponentDescriptor>(
          "AndroidSwipeRefreshLayout",
          RuntimeCapabilityClass::Implemented,
          owner,
          "skia-refresh-control"));
  inventory.officialComponents.push_back(unimplementedComponent(
      "PullToRefreshView",
      RuntimeCapabilityClass::LayoutOnly,
      owner,
      "layout-only-placeholder"));
  inventory.officialComponents.push_back(unimplementedComponent(
      "AndroidHorizontalScrollView",
      RuntimeCapabilityClass::HostAdapted,
      owner,
      "headless-viewport-state"));
  inventory.officialComponents.push_back(
      realComponent<react::AndroidHorizontalScrollContentViewComponentDescriptor>(
          "AndroidHorizontalScrollContentView",
          RuntimeCapabilityClass::Implemented,
          owner,
          "real-fabric-yoga"));
  inventory.officialComponents.push_back(
      realComponent<react::HeadlessSafeAreaViewComponentDescriptor>(
          "SafeAreaView",
          RuntimeCapabilityClass::HostAdapted,
          owner,
          "window-relative-insets"));
  inventory.officialComponents.push_back(unimplementedComponent(
      "InputAccessory",
      RuntimeCapabilityClass::LayoutOnly,
      owner,
      "layout-only-placeholder"));
  inventory.officialComponents.push_back(unimplementedComponent(
      "VirtualView",
      RuntimeCapabilityClass::LayoutOnly,
      owner,
      "layout-only-placeholder"));
  inventory.officialComponents.push_back(unimplementedComponent(
      "VirtualViewExperimental",
      RuntimeCapabilityClass::LayoutOnly,
      owner,
      "layout-only-placeholder"));
  inventory.officialComponents.push_back(unimplementedComponent(
      "DebuggingOverlay",
      RuntimeCapabilityClass::LayoutOnly,
      owner,
      "layout-only-placeholder"));
  inventory.officialComponents.push_back(unimplementedComponent(
      "RCTImageView",
      RuntimeCapabilityClass::LayoutOnly,
      owner,
      "layout-only-placeholder"));

  if (inventory.profile.platform == "ios") {
    inventory.platformComponents.push_back(
        realComponent<react::HeadlessIOSTextInputComponentDescriptor>(
            "TextInput",
            RuntimeCapabilityClass::HostAdapted,
            owner,
            "rn-fabric-headless-platform-adapter"));
  } else {
    inventory.platformComponents.push_back(
        realComponent<react::HeadlessAndroidTextInputComponentDescriptor>(
            "AndroidTextInput",
            RuntimeCapabilityClass::HostAdapted,
            owner,
            "rn-fabric-headless-platform-adapter"));
  }

  return inventory;
}

std::vector<FrameworkComponentEntry> allInventoryComponents(
    const FrameworkSurfaceInventory& inventory) {
  std::vector<FrameworkComponentEntry> result;
  result.insert(
      result.end(),
      inventory.baseComponents.begin(),
      inventory.baseComponents.end());
  result.insert(
      result.end(),
      inventory.officialComponents.begin(),
      inventory.officialComponents.end());
  result.insert(
      result.end(),
      inventory.platformComponents.begin(),
      inventory.platformComponents.end());
  return result;
}

} // namespace ReactNativeSimulator
