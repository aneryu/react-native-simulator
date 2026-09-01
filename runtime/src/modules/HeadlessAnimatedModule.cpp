#include "HeadlessAnimatedModule.h"
#include "SimulatorEventLoop.h"
#include "HeadlessReactFabric.h"

#include <ReactCommon/TurboModuleWithJSIBindings.h>
#include <glog/logging.h>
#include <jsi/JSIDynamic.h>
#include <react/renderer/animated/NativeAnimatedNodesManager.h>
#include <react/renderer/animationbackend/AnimationBackend.h>
#include <react/renderer/animationbackend/AnimationChoreographer.h>
#include <react/renderer/bridging/bridging.h>
#include <react/renderer/core/RawEvent.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/uimanager/UIManager.h>
#include <react/renderer/uimanager/UIManagerBinding.h>
#include <react/renderer/uimanager/UIManagerNativeAnimatedDelegate.h>

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jsi = facebook::jsi;
namespace react = facebook::react;

namespace {
class HeadlessAnimationChoreographer final
    : public react::AnimationChoreographer {
 public:
  explicit HeadlessAnimationChoreographer(
      std::shared_ptr<SimulatorEventLoop> eventLoop)
      : eventLoop_(std::move(eventLoop)) {}

  void resume() override {
    if (!eventLoop_) {
      return;
    }
    eventLoop_->startDisplayLink([this] {
      onAnimationFrame(now());
    });
  }

  void pause() override {
    if (eventLoop_) {
      eventLoop_->stopDisplayLink();
    }
  }

 private:
  std::shared_ptr<SimulatorEventLoop> eventLoop_;
};

class HeadlessAnimatedDelegate final
    : public react::UIManagerNativeAnimatedDelegate {
 public:
  explicit HeadlessAnimatedDelegate(
      std::weak_ptr<react::NativeAnimatedNodesManager> manager)
      : manager_(std::move(manager)) {}

  void runAnimationFrame() override {
    if (auto manager = manager_.lock()) {
      manager->onRender();
    }
  }

 private:
  std::weak_ptr<react::NativeAnimatedNodesManager> manager_;
};

class HeadlessAnimatedModule final : public react::TurboModule,
                                     public react::TurboModuleWithJSIBindings {
 public:
  HeadlessAnimatedModule(
      std::shared_ptr<react::CallInvoker> jsInvoker,
      std::shared_ptr<SimulatorEventLoop> eventLoop)
      : TurboModule("NativeAnimatedModule", std::move(jsInvoker)),
        eventLoop_(std::move(eventLoop)) {
    methodMap_["startOperationBatch"] = {0, &invokeNoop};
    methodMap_["finishOperationBatch"] = {0, &invokeFinishOperationBatch};
    methodMap_["createAnimatedNode"] = {2, &invokeCreateAnimatedNode};
    methodMap_["updateAnimatedNodeConfig"] = {2, &invokeNoop};
    methodMap_["getValue"] = {2, &invokeGetValue};
    methodMap_["startListeningToAnimatedNodeValue"] = {
        1, &invokeStartListening};
    methodMap_["stopListeningToAnimatedNodeValue"] = {1, &invokeStopListening};
    methodMap_["connectAnimatedNodes"] = {2, &invokeConnectNodes};
    methodMap_["disconnectAnimatedNodes"] = {2, &invokeDisconnectNodes};
    methodMap_["startAnimatingNode"] = {4, &invokeStartAnimatingNode};
    methodMap_["stopAnimation"] = {1, &invokeStopAnimation};
    methodMap_["setAnimatedNodeValue"] = {2, &invokeSetValue};
    methodMap_["setAnimatedNodeOffset"] = {2, &invokeSetOffset};
    methodMap_["flattenAnimatedNodeOffset"] = {1, &invokeFlattenOffset};
    methodMap_["extractAnimatedNodeOffset"] = {1, &invokeExtractOffset};
    methodMap_["connectAnimatedNodeToView"] = {2, &invokeConnectToView};
    methodMap_["connectAnimatedNodeToShadowNodeFamily"] = {
        2, &invokeConnectToFamily};
    methodMap_["disconnectAnimatedNodeFromView"] = {
        2, &invokeDisconnectFromView};
    methodMap_["restoreDefaultValues"] = {1, &invokeRestoreDefaultValues};
    methodMap_["dropAnimatedNode"] = {1, &invokeDropAnimatedNode};
    methodMap_["addAnimatedEventToView"] = {3, &invokeAddAnimatedEvent};
    methodMap_["removeAnimatedEventFromView"] = {3, &invokeRemoveAnimatedEvent};
    methodMap_["addListener"] = {1, &invokeNoop};
    methodMap_["removeListeners"] = {1, &invokeNoop};
    methodMap_["queueAndExecuteBatchedOperations"] = {1, &invokeNoop};
  }

 private:
  std::shared_ptr<SimulatorEventLoop> eventLoop_;
  std::shared_ptr<HeadlessAnimationChoreographer> choreographer_;
  std::shared_ptr<react::AnimationBackend> animationBackend_;
  std::weak_ptr<react::NativeAnimatedNodesManager> nodesManager_;
  std::shared_ptr<HeadlessAnimatedDelegate> delegate_;
  std::vector<std::function<void(react::NativeAnimatedNodesManager&)>>
      preOperations_;
  std::vector<std::function<void(react::NativeAnimatedNodesManager&)>>
      operations_;

  static HeadlessAnimatedModule& self(react::TurboModule& module) {
    return static_cast<HeadlessAnimatedModule&>(module);
  }

  static react::Tag tagAt(const jsi::Value& value) {
    return static_cast<react::Tag>(value.getNumber());
  }

  void enqueue(
      std::function<void(react::NativeAnimatedNodesManager&)> operation,
      bool pre = false) {
    (pre ? preOperations_ : operations_).push_back(std::move(operation));
  }

  void finishOperationBatch() {
    std::vector<std::function<void(react::NativeAnimatedNodesManager&)>>
        preOperations;
    std::vector<std::function<void(react::NativeAnimatedNodesManager&)>>
        operations;
    std::swap(preOperations_, preOperations);
    std::swap(operations_, operations);
    if (auto nodesManager = nodesManager_.lock()) {
      nodesManager->scheduleOnUI(
          [preOperations = std::move(preOperations),
           operations = std::move(operations),
           nodesManager]() {
            for (const auto& operation : preOperations) {
              operation(*nodesManager);
            }
            for (const auto& operation : operations) {
              operation(*nodesManager);
            }
          });
    }
  }

  void installJSIBindingsWithRuntime(jsi::Runtime& runtime) override {
    auto binding = react::UIManagerBinding::getBinding(runtime);
    if (binding == nullptr) {
      LOG(ERROR) << "NativeAnimatedModule installed before UIManagerBinding";
      return;
    }
    auto& uiManager = binding->getUIManager();
    auto uiManagerPtr = getHeadlessReactFabricUIManager();
    if (uiManagerPtr == nullptr || uiManagerPtr.get() != &uiManager) {
      LOG(ERROR) << "NativeAnimatedModule could not retain Fabric UIManager";
      return;
    }
    choreographer_ =
        std::make_shared<HeadlessAnimationChoreographer>(eventLoop_);
    animationBackend_ = std::make_shared<react::AnimationBackend>(
        choreographer_, uiManagerPtr);
    choreographer_->setAnimationBackend(animationBackend_);
    animationBackend_->registerJSInvoker(jsInvoker_);
    uiManager.unstable_setAnimationBackend(animationBackend_);
    auto manager =
        std::make_shared<react::NativeAnimatedNodesManager>(animationBackend_);
    nodesManager_ = manager;
    delegate_ = std::make_shared<HeadlessAnimatedDelegate>(manager);
    uiManager.setNativeAnimatedDelegate(delegate_);
    uiManager.addEventListener(std::make_shared<react::EventListener>(
        [manager](const react::RawEvent& rawEvent) {
          const auto& eventTarget = rawEvent.eventTarget;
          const auto& eventPayload = rawEvent.eventPayload;
          if (!eventTarget || !eventPayload) {
            return false;
          }
          auto listener = manager->getEventEmitterListener();
          return listener &&
              (*listener)(eventTarget->getTag(), rawEvent.type, *eventPayload);
        }));
  }

  static jsi::Value invokeNoop(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Value::undefined();
  }

  static jsi::Value invokeFinishOperationBatch(
      jsi::Runtime&,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    self(module).finishOperationBatch();
    return jsi::Value::undefined();
  }

  static jsi::Value invokeCreateAnimatedNode(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 2 || !args[0].isNumber() || !args[1].isObject()) {
      throw jsi::JSError(rt, "createAnimatedNode expects tag and config");
    }
    auto config = facebook::jsi::dynamicFromValue(rt, args[1]);
    const auto tag = tagAt(args[0]);
    if (auto it = config.find("disableBatchingForNativeCreate");
        it != config.items().end() && it->second == true) {
      if (auto nodesManager = self(module).nodesManager_.lock()) {
        nodesManager->createAnimatedNodeAsync(tag, config);
      }
    } else {
      self(module).enqueue([tag, config = std::move(config)](auto& manager) {
        manager.createAnimatedNode(tag, config);
      });
    }
    return jsi::Value::undefined();
  }

  static jsi::Value invokeGetValue(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 2 || !args[0].isNumber() || !args[1].isObject() ||
        !args[1].asObject(rt).isFunction(rt)) {
      throw jsi::JSError(rt, "getValue expects tag and callback");
    }
    react::AsyncCallback<double> callback(
        rt, args[1].asObject(rt).asFunction(rt), self(module).jsInvoker_);
    const auto tag = tagAt(args[0]);
    self(module).enqueue(
        [tag, callback = std::move(callback)](auto& manager) mutable {
          if (auto value = manager.getValue(tag)) {
            callback.call(*value);
          }
        });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeStartListening(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isNumber()) {
      throw jsi::JSError(rt, "startListeningToAnimatedNodeValue expects a tag");
    }
    const auto tag = tagAt(args[0]);
    auto* animated = &self(module);
    self(module).enqueue([animated, tag](auto& manager) {
      manager.startListeningToAnimatedNodeValue(
          tag, [animated, tag](double value) {
            animated->emitDeviceEvent(
                "onAnimatedValueUpdate",
                [tag, value](jsi::Runtime& rt, std::vector<jsi::Value>& args) {
                  auto payload = jsi::Object(rt);
                  payload.setProperty(rt, "tag", jsi::Value(tag));
                  payload.setProperty(rt, "value", jsi::Value(value));
                  args.emplace_back(rt, payload);
                });
          });
    });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeStopListening(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isNumber()) {
      throw jsi::JSError(rt, "stopListeningToAnimatedNodeValue expects a tag");
    }
    const auto tag = tagAt(args[0]);
    self(module).enqueue([tag](auto& manager) {
      manager.stopListeningToAnimatedNodeValue(tag);
    });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeConnectNodes(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 2 || !args[0].isNumber() || !args[1].isNumber()) {
      throw jsi::JSError(rt, "connectAnimatedNodes expects two tags");
    }
    const auto parentTag = tagAt(args[0]);
    const auto childTag = tagAt(args[1]);
    self(module).enqueue([parentTag, childTag](auto& manager) {
      manager.connectAnimatedNodes(parentTag, childTag);
    });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeDisconnectNodes(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 2 || !args[0].isNumber() || !args[1].isNumber()) {
      throw jsi::JSError(rt, "disconnectAnimatedNodes expects two tags");
    }
    const auto parentTag = tagAt(args[0]);
    const auto childTag = tagAt(args[1]);
    self(module).enqueue([parentTag, childTag](auto& manager) {
      manager.disconnectAnimatedNodes(parentTag, childTag);
    });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeStartAnimatingNode(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 4 || !args[0].isNumber() || !args[1].isNumber() ||
        !args[2].isObject() || !args[3].isObject() ||
        !args[3].asObject(rt).isFunction(rt)) {
      throw jsi::JSError(
          rt, "startAnimatingNode expects id, tag, config, and end callback");
    }
    const auto animationId = static_cast<int>(args[0].getNumber());
    const auto nodeTag = tagAt(args[1]);
    auto config = facebook::jsi::dynamicFromValue(rt, args[2]);
    react::AnimationEndCallback endCallback(
        rt, args[3].asObject(rt).asFunction(rt), self(module).jsInvoker_);
    self(module).enqueue(
        [animationId,
         nodeTag,
         config = std::move(config),
         endCallback = std::move(endCallback)](auto& manager) mutable {
          manager.startAnimatingNode(
              animationId, nodeTag, std::move(config), std::move(endCallback));
        });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeStopAnimation(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isNumber()) {
      throw jsi::JSError(rt, "stopAnimation expects an animation id");
    }
    const auto animationId = static_cast<int>(args[0].getNumber());
    self(module).enqueue([animationId](auto& manager) {
      manager.stopAnimation(animationId);
    });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeSetValue(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 2 || !args[0].isNumber() || !args[1].isNumber()) {
      throw jsi::JSError(rt, "setAnimatedNodeValue expects tag and value");
    }
    const auto nodeTag = tagAt(args[0]);
    const auto value = args[1].getNumber();
    self(module).enqueue([nodeTag, value](auto& manager) {
      manager.setAnimatedNodeValue(nodeTag, value);
    });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeSetOffset(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 2 || !args[0].isNumber() || !args[1].isNumber()) {
      throw jsi::JSError(rt, "setAnimatedNodeOffset expects tag and offset");
    }
    const auto nodeTag = tagAt(args[0]);
    const auto offset = args[1].getNumber();
    self(module).enqueue([nodeTag, offset](auto& manager) {
      manager.setAnimatedNodeOffset(nodeTag, offset);
    });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeFlattenOffset(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isNumber()) {
      throw jsi::JSError(rt, "flattenAnimatedNodeOffset expects a tag");
    }
    const auto nodeTag = tagAt(args[0]);
    self(module).enqueue([nodeTag](auto& manager) {
      manager.flattenAnimatedNodeOffset(nodeTag);
    });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeExtractOffset(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isNumber()) {
      throw jsi::JSError(rt, "extractAnimatedNodeOffset expects a tag");
    }
    const auto nodeTag = tagAt(args[0]);
    self(module).enqueue([nodeTag](auto& manager) {
      manager.extractAnimatedNodeOffsetOp(nodeTag);
    });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeConnectToView(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 2 || !args[0].isNumber() || !args[1].isNumber()) {
      throw jsi::JSError(rt, "connectAnimatedNodeToView expects two tags");
    }
    const auto nodeTag = tagAt(args[0]);
    const auto viewTag = tagAt(args[1]);
    self(module).enqueue([nodeTag, viewTag](auto& manager) {
      manager.connectAnimatedNodeToView(nodeTag, viewTag);
      if (auto uiManager = getHeadlessReactFabricUIManager()) {
        if (auto shadowNode =
                uiManager->findShadowNodeByTag_DEPRECATED(viewTag)) {
          manager.connectAnimatedNodeToShadowNodeFamily(
              nodeTag, shadowNode->getFamilyShared());
        }
      }
    });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeConnectToFamily(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 2 || !args[0].isNumber() || !args[1].isObject()) {
      throw jsi::JSError(
          rt, "connectAnimatedNodeToShadowNodeFamily expects tag and node");
    }
    const auto nodeTag = tagAt(args[0]);
    auto shadowNode =
        react::Bridging<std::shared_ptr<const react::ShadowNode>>::fromJs(
            rt, args[1]);
    auto family = shadowNode->getFamilyShared();
    self(module).enqueue(
        [nodeTag, family = std::move(family)](auto& manager) {
          manager.connectAnimatedNodeToShadowNodeFamily(nodeTag, family);
        });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeDisconnectFromView(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 2 || !args[0].isNumber() || !args[1].isNumber()) {
      throw jsi::JSError(rt, "disconnectAnimatedNodeFromView expects two tags");
    }
    const auto nodeTag = tagAt(args[0]);
    const auto viewTag = tagAt(args[1]);
    self(module).enqueue([nodeTag, viewTag](auto& manager) {
      manager.disconnectAnimatedNodeFromView(nodeTag, viewTag);
    });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeRestoreDefaultValues(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isNumber()) {
      throw jsi::JSError(rt, "restoreDefaultValues expects a tag");
    }
    const auto nodeTag = tagAt(args[0]);
    self(module).enqueue(
        [nodeTag](auto& manager) { manager.restoreDefaultValues(nodeTag); },
        true);
    return jsi::Value::undefined();
  }

  static jsi::Value invokeDropAnimatedNode(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isNumber()) {
      throw jsi::JSError(rt, "dropAnimatedNode expects a tag");
    }
    const auto tag = tagAt(args[0]);
    self(module).enqueue(
        [tag](auto& manager) { manager.dropAnimatedNode(tag); });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeAddAnimatedEvent(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 3 || !args[0].isNumber() || !args[1].isString() ||
        !args[2].isObject()) {
      throw jsi::JSError(
          rt, "addAnimatedEventToView expects tag, event name, and mapping");
    }
    const auto viewTag = tagAt(args[0]);
    auto eventName = args[1].asString(rt).utf8(rt);
    auto eventMapping = facebook::jsi::dynamicFromValue(rt, args[2]);
    self(module).enqueue(
        [viewTag,
         eventName = std::move(eventName),
         eventMapping = std::move(eventMapping)](auto& manager) {
          manager.addAnimatedEventToView(viewTag, eventName, eventMapping);
        });
    return jsi::Value::undefined();
  }

  static jsi::Value invokeRemoveAnimatedEvent(
      jsi::Runtime& rt,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 3 || !args[0].isNumber() || !args[1].isString() ||
        !args[2].isNumber()) {
      throw jsi::JSError(
          rt,
          "removeAnimatedEventFromView expects tag, event name, and node tag");
    }
    const auto viewTag = tagAt(args[0]);
    auto eventName = args[1].asString(rt).utf8(rt);
    const auto animatedNodeTag = tagAt(args[2]);
    self(module).enqueue(
        [viewTag, eventName = std::move(eventName), animatedNodeTag](
            auto& manager) {
          manager.removeAnimatedEventFromView(
              viewTag, eventName, animatedNodeTag);
        });
    return jsi::Value::undefined();
  }
};
} // namespace

std::shared_ptr<react::TurboModule> createHeadlessAnimatedModule(
    std::shared_ptr<react::CallInvoker> jsInvoker,
    std::shared_ptr<SimulatorEventLoop> eventLoop) {
  return std::make_shared<HeadlessAnimatedModule>(
      std::move(jsInvoker), std::move(eventLoop));
}
