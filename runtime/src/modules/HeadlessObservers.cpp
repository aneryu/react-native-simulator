#include "HeadlessObservers.h"

#include <react/renderer/bridging/bridging.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/core/ShadowNodeFamily.h>
#include <react/renderer/graphics/Rect.h>
#include <react/timing/primitives.h>
#include <react/renderer/observers/intersection/IntersectionObserverManager.h>
#include <react/renderer/observers/mutation/MutationObserverManager.h>
#include <react/renderer/runtimescheduler/RuntimeSchedulerBinding.h>
#include <react/renderer/uimanager/UIManager.h>
#include <react/renderer/uimanager/UIManagerBinding.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace jsi = facebook::jsi;
namespace react = facebook::react;

namespace {
react::UIManager& uiManagerOf(jsi::Runtime& runtime) {
  auto binding = react::UIManagerBinding::getBinding(runtime);
  if (binding == nullptr) {
    throw jsi::JSError(runtime, "Fabric UIManagerBinding is required");
  }
  return binding->getUIManager();
}

react::RuntimeScheduler& runtimeSchedulerOf(jsi::Runtime& runtime) {
  auto binding = react::RuntimeSchedulerBinding::getBinding(runtime);
  if (binding == nullptr) {
    throw jsi::JSError(runtime, "RuntimeSchedulerBinding is required");
  }
  return *binding->getRuntimeScheduler();
}

std::shared_ptr<const react::ShadowNode> shadowNodeFromJs(
    jsi::Runtime& runtime,
    const jsi::Value& value) {
  return react::Bridging<std::shared_ptr<const react::ShadowNode>>::fromJs(
      runtime, value);
}

std::vector<react::Float> floatArrayFromJs(
    jsi::Runtime& runtime,
    const jsi::Value& value) {
  std::vector<react::Float> values;
  if (!value.isObject()) {
    return values;
  }
  auto array = value.getObject(runtime).asArray(runtime);
  const auto size = array.size(runtime);
  values.reserve(size);
  for (size_t index = 0; index < size; ++index) {
    const auto item = array.getValueAtIndex(runtime, index);
    if (item.isNumber()) {
      values.push_back(static_cast<react::Float>(item.getNumber()));
    }
  }
  return values;
}

jsi::Array rectToArray(jsi::Runtime& runtime, const react::Rect& rect) {
  jsi::Array array(runtime, 4);
  array.setValueAtIndex(runtime, 0, rect.origin.x);
  array.setValueAtIndex(runtime, 1, rect.origin.y);
  array.setValueAtIndex(runtime, 2, rect.size.width);
  array.setValueAtIndex(runtime, 3, rect.size.height);
  return array;
}

class HeadlessIntersectionObserverTurboModule final : public react::TurboModule {
 public:
  explicit HeadlessIntersectionObserverTurboModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("NativeIntersectionObserverCxx", std::move(jsInvoker)) {
    methodMap_["observeV2"] = {1, &observeV2};
    methodMap_["unobserveV2"] = {2, &unobserveV2};
    methodMap_["connect"] = {1, &connect};
    methodMap_["disconnect"] = {0, &disconnect};
    methodMap_["takeRecords"] = {0, &takeRecords};
  }

 private:
  react::IntersectionObserverManager manager_;

  static HeadlessIntersectionObserverTurboModule& self(
      react::TurboModule& module) {
    return static_cast<HeadlessIntersectionObserverTurboModule&>(module);
  }

  static jsi::Value observeV2(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isObject()) {
      throw jsi::JSError(
          runtime, "observeV2 expects an options object");
    }
    auto options = args[0].getObject(runtime);
    const auto idValue = options.getProperty(runtime, "intersectionObserverId");
    const auto targetValue = options.getProperty(runtime, "targetShadowNode");
    if (!idValue.isNumber() || targetValue.isUndefined() ||
        targetValue.isNull()) {
      throw jsi::JSError(
          runtime, "observeV2 requires intersectionObserverId and targetShadowNode");
    }
    const auto observerId =
        static_cast<react::IntersectionObserverObserverId>(idValue.getNumber());
    auto target = shadowNodeFromJs(runtime, targetValue);
    auto family = target->getFamilyShared();

    std::optional<react::ShadowNodeFamily::Shared> rootFamily;
    const auto rootValue = options.getProperty(runtime, "rootShadowNode");
    if (!rootValue.isUndefined() && !rootValue.isNull()) {
      rootFamily = shadowNodeFromJs(runtime, rootValue)->getFamilyShared();
    }

    std::optional<std::vector<react::Float>> rootThresholds;
    const auto rootThresholdsValue =
        options.getProperty(runtime, "rootThresholds");
    if (!rootThresholdsValue.isUndefined() && !rootThresholdsValue.isNull()) {
      rootThresholds = floatArrayFromJs(runtime, rootThresholdsValue);
    }

    std::optional<std::string> rootMargin;
    const auto marginValue = options.getProperty(runtime, "rootMargin");
    if (marginValue.isString()) {
      rootMargin = marginValue.getString(runtime).utf8(runtime);
    }

    self(module).manager_.observe(
        observerId,
        rootFamily,
        family,
        floatArrayFromJs(
            runtime, options.getProperty(runtime, "thresholds")),
        rootThresholds,
        rootMargin,
        uiManagerOf(runtime));

    jsi::Object token(runtime);
    token.setNativeState(
        runtime,
        std::const_pointer_cast<react::ShadowNodeFamily>(std::move(family)));
    return token;
  }

  static jsi::Value unobserveV2(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 2 || !args[0].isNumber() || !args[1].isObject()) {
      return jsi::Value::undefined();
    }
    auto family =
        args[1].getObject(runtime).getNativeState<react::ShadowNodeFamily>(
            runtime);
    if (family) {
      self(module).manager_.unobserve(
          static_cast<react::IntersectionObserverObserverId>(
              args[0].getNumber()),
          family);
    }
    return jsi::Value::undefined();
  }

  static jsi::Value connect(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isObject() ||
        !args[0].getObject(runtime).isFunction(runtime)) {
      throw jsi::JSError(runtime, "connect expects a notify callback");
    }
    auto notify = std::make_shared<jsi::Function>(
        args[0].getObject(runtime).getFunction(runtime));
    auto jsInvoker = self(module).jsInvoker_;
    self(module).manager_.connect(
        runtimeSchedulerOf(runtime),
        uiManagerOf(runtime),
        [jsInvoker, notify]() {
          if (!jsInvoker) {
            return;
          }
          jsInvoker->invokeAsync([notify](jsi::Runtime& runtime) {
            notify->call(runtime);
          });
        });
    return jsi::Value::undefined();
  }

  static jsi::Value disconnect(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    self(module).manager_.disconnect(
        runtimeSchedulerOf(runtime), uiManagerOf(runtime));
    return jsi::Value::undefined();
  }

  static jsi::Value takeRecords(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    const auto entries = self(module).manager_.takeRecords();
    jsi::Array array(runtime, entries.size());
    for (size_t index = 0; index < entries.size(); ++index) {
      const auto& entry = entries[index];
      jsi::Object object(runtime);
      object.setProperty(
          runtime, "intersectionObserverId", entry.intersectionObserverId);
      object.setProperty(
          runtime,
          "targetInstanceHandle",
          entry.shadowNodeFamily
              ? entry.shadowNodeFamily->getInstanceHandle(runtime)
              : jsi::Value::null());
      object.setProperty(
          runtime, "targetRect", rectToArray(runtime, entry.targetRect));
      object.setProperty(
          runtime, "rootRect", rectToArray(runtime, entry.rootRect));
      object.setProperty(
          runtime,
          "intersectionRect",
          rectToArray(runtime, entry.intersectionRect));
      object.setProperty(
          runtime,
          "isIntersectingAboveThresholds",
          entry.isIntersectingAboveThresholds);
      object.setProperty(
          runtime, "time", entry.time.toDOMHighResTimeStamp());
      array.setValueAtIndex(runtime, index, std::move(object));
    }
    return array;
  }
};

class HeadlessMutationObserverTurboModule final : public react::TurboModule {
 public:
  explicit HeadlessMutationObserverTurboModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("NativeMutationObserverCxx", std::move(jsInvoker)) {
    methodMap_["observe"] = {1, &observe};
    methodMap_["unobserveAll"] = {1, &unobserveAll};
    methodMap_["connect"] = {2, &connect};
    methodMap_["disconnect"] = {0, &disconnect};
    methodMap_["takeRecords"] = {0, &takeRecords};
  }

 private:
  react::MutationObserverManager manager_;
  jsi::Runtime* runtime_{nullptr};
  std::shared_ptr<jsi::Function> notify_;
  std::shared_ptr<jsi::Function> getPublicInstance_;
  std::vector<jsi::Value> pendingRecords_;
  bool notified_{false};

  static HeadlessMutationObserverTurboModule& self(
      react::TurboModule& module) {
    return static_cast<HeadlessMutationObserverTurboModule&>(module);
  }

  jsi::Value publicInstanceFromShadowNode(
      const react::ShadowNode& shadowNode) const {
    if (runtime_ == nullptr || !getPublicInstance_) {
      return jsi::Value::null();
    }
    auto handle = shadowNode.getInstanceHandle(*runtime_);
    if (!handle.isObject()) {
      return jsi::Value::null();
    }
    return getPublicInstance_->call(*runtime_, std::move(handle));
  }

  jsi::Array publicInstancesFromShadowNodes(
      const std::vector<std::shared_ptr<const react::ShadowNode>>& nodes)
      const {
    jsi::Array array(*runtime_, nodes.size());
    for (size_t index = 0; index < nodes.size(); ++index) {
      array.setValueAtIndex(
          *runtime_,
          index,
          nodes[index]
              ? publicInstanceFromShadowNode(*nodes[index])
              : jsi::Value::null());
    }
    return array;
  }

  void onMutations(std::vector<react::MutationRecord>& records) {
    if (runtime_ == nullptr) {
      return;
    }
    for (const auto& record : records) {
      jsi::Object object(*runtime_);
      object.setProperty(
          *runtime_, "mutationObserverId", record.mutationObserverId);
      object.setProperty(
          *runtime_,
          "target",
          record.targetShadowNode
              ? publicInstanceFromShadowNode(*record.targetShadowNode)
              : jsi::Value::null());
      object.setProperty(
          *runtime_,
          "addedNodes",
          publicInstancesFromShadowNodes(record.addedShadowNodes));
      object.setProperty(
          *runtime_,
          "removedNodes",
          publicInstancesFromShadowNodes(record.removedShadowNodes));
      pendingRecords_.emplace_back(std::move(object));
    }
    if (!pendingRecords_.empty() && !notified_ && notify_) {
      notified_ = true;
      runtime_->queueMicrotask(*notify_);
    }
  }

  static jsi::Value observe(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isObject()) {
      throw jsi::JSError(runtime, "observe expects an options object");
    }
    auto options = args[0].getObject(runtime);
    const auto idValue = options.getProperty(runtime, "mutationObserverId");
    const auto targetValue = options.getProperty(runtime, "targetShadowNode");
    if (!idValue.isNumber() || targetValue.isUndefined() ||
        targetValue.isNull()) {
      throw jsi::JSError(
          runtime, "observe requires mutationObserverId and targetShadowNode");
    }
    const auto subtreeValue = options.getProperty(runtime, "subtree");
    self(module).manager_.observe(
        static_cast<react::MutationObserverId>(idValue.getNumber()),
        shadowNodeFromJs(runtime, targetValue),
        subtreeValue.isBool() && subtreeValue.getBool(),
        uiManagerOf(runtime));
    return jsi::Value::undefined();
  }

  static jsi::Value unobserveAll(
      jsi::Runtime&,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isNumber()) {
      return jsi::Value::undefined();
    }
    self(module).manager_.unobserveAll(
        static_cast<react::MutationObserverId>(args[0].getNumber()));
    return jsi::Value::undefined();
  }

  static jsi::Value connect(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 2 || !args[0].isObject() ||
        !args[0].getObject(runtime).isFunction(runtime) ||
        !args[1].isObject() ||
        !args[1].getObject(runtime).isFunction(runtime)) {
      throw jsi::JSError(
          runtime,
          "connect expects notify and getPublicInstance callbacks");
    }
    auto& observer = self(module);
    observer.runtime_ = &runtime;
    observer.notify_ = std::make_shared<jsi::Function>(
        args[0].getObject(runtime).getFunction(runtime));
    observer.getPublicInstance_ = std::make_shared<jsi::Function>(
        args[1].getObject(runtime).getFunction(runtime));
    observer.notified_ = false;
    observer.manager_.connect(
        uiManagerOf(runtime),
        [&observer](std::vector<react::MutationRecord>& records) {
          observer.onMutations(records);
        });
    return jsi::Value::undefined();
  }

  static jsi::Value disconnect(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    auto& observer = self(module);
    observer.manager_.disconnect(uiManagerOf(runtime));
    observer.runtime_ = nullptr;
    observer.notify_.reset();
    observer.getPublicInstance_.reset();
    observer.pendingRecords_.clear();
    observer.notified_ = false;
    return jsi::Value::undefined();
  }

  static jsi::Value takeRecords(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    auto& observer = self(module);
    observer.notified_ = false;
    jsi::Array array(runtime, observer.pendingRecords_.size());
    for (size_t index = 0; index < observer.pendingRecords_.size(); ++index) {
      array.setValueAtIndex(
          runtime, index, std::move(observer.pendingRecords_[index]));
    }
    observer.pendingRecords_.clear();
    return array;
  }
};
} // namespace

std::shared_ptr<facebook::react::TurboModule>
createHeadlessIntersectionObserver(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
  return std::make_shared<HeadlessIntersectionObserverTurboModule>(
      std::move(jsInvoker));
}

std::shared_ptr<facebook::react::TurboModule> createHeadlessMutationObserver(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
  return std::make_shared<HeadlessMutationObserverTurboModule>(
      std::move(jsInvoker));
}
