#include "HeadlessNativeDOM.h"

#include <react/renderer/bridging/bridging.h>
#include <react/renderer/components/root/RootShadowNode.h>
#include <react/renderer/core/InstanceHandle.h>
#include <react/renderer/core/RawProps.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/dom/DOM.h>
#include <react/renderer/uimanager/PointerEventsProcessor.h>
#include <react/renderer/uimanager/UIManager.h>
#include <react/renderer/uimanager/UIManagerBinding.h>

#include <stdexcept>
#include <utility>
#include <vector>

namespace jsi = facebook::jsi;
namespace react = facebook::react;

namespace {
react::UIManagerBinding& bindingOf(jsi::Runtime& runtime) {
  auto binding = react::UIManagerBinding::getBinding(runtime);
  if (binding == nullptr) {
    throw jsi::JSError(
        runtime, "NativeDOMCxx requires Fabric UIManagerBinding");
  }
  return *binding;
}

react::RootShadowNode::Shared currentRevision(
    jsi::Runtime& runtime,
    react::SurfaceId surfaceId) {
  auto* provider = bindingOf(runtime).getUIManager().getShadowTreeRevisionProvider();
  if (provider == nullptr) {
    return nullptr;
  }
  return provider->getCurrentRevision(surfaceId);
}

std::shared_ptr<const react::ShadowNode> shadowNodeFromJs(
    jsi::Runtime& runtime,
    const jsi::Value& value) {
  return react::Bridging<std::shared_ptr<const react::ShadowNode>>::fromJs(
      runtime, value);
}

react::RootShadowNode::Shared currentRevisionForReference(
    jsi::Runtime& runtime,
    const jsi::Value& nativeNodeReference) {
  if (nativeNodeReference.isNumber()) {
    return currentRevision(
        runtime,
        static_cast<react::SurfaceId>(nativeNodeReference.getNumber()));
  }
  return currentRevision(
      runtime, shadowNodeFromJs(runtime, nativeNodeReference)->getSurfaceId());
}

bool isRootShadowNode(const react::ShadowNode& shadowNode) {
  return shadowNode.getTraits().check(react::ShadowNodeTraits::Trait::RootNodeKind);
}

std::vector<jsi::Value> instanceHandlesFromNodes(
    jsi::Runtime& runtime,
    const std::vector<std::shared_ptr<const react::ShadowNode>>& nodes) {
  std::vector<jsi::Value> handles;
  handles.reserve(nodes.size());
  for (const auto& node : nodes) {
    auto handle = node->getInstanceHandle(runtime);
    if (!handle.isNull()) {
      handles.push_back(std::move(handle));
    }
  }
  return handles;
}

jsi::Value toJsArray(jsi::Runtime& runtime, std::vector<jsi::Value> values) {
  jsi::Array array(runtime, values.size());
  for (size_t index = 0; index < values.size(); ++index) {
    array.setValueAtIndex(runtime, index, std::move(values[index]));
  }
  return array;
}

jsi::Value numberArray(
    jsi::Runtime& runtime,
    std::initializer_list<double> values) {
  jsi::Array array(runtime, values.size());
  size_t index = 0;
  for (const auto value : values) {
    array.setValueAtIndex(runtime, index++, value);
  }
  return array;
}

void callNumbers(
    jsi::Runtime& runtime,
    const jsi::Value& callback,
    std::initializer_list<double> values) {
  if (!callback.isObject() || !callback.getObject(runtime).isFunction(runtime)) {
    return;
  }
  std::vector<jsi::Value> args;
  args.reserve(values.size());
  for (const auto value : values) {
    args.emplace_back(value);
  }
  auto function = callback.getObject(runtime).getFunction(runtime);
  switch (args.size()) {
    case 0:
      function.call(runtime);
      break;
    case 4:
      function.call(runtime, args[0], args[1], args[2], args[3]);
      break;
    case 6:
      function.call(
          runtime, args[0], args[1], args[2], args[3], args[4], args[5]);
      break;
    default:
      throw jsi::JSError(runtime, "NativeDOMCxx callback arity is unsupported");
  }
}

class HeadlessNativeDOMTurboModule final : public react::TurboModule {
 public:
  explicit HeadlessNativeDOMTurboModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("NativeDOMCxx", std::move(jsInvoker)) {
    methodMap_["compareDocumentPosition"] = {2, &compareDocumentPosition};
    methodMap_["getChildNodes"] = {1, &getChildNodes};
    methodMap_["getElementById"] = {2, &getElementById};
    methodMap_["getParentNode"] = {1, &getParentNode};
    methodMap_["isConnected"] = {1, &isConnected};
    methodMap_["getBorderWidth"] = {1, &getBorderWidth};
    methodMap_["getBoundingClientRect"] = {2, &getBoundingClientRect};
    methodMap_["getInnerSize"] = {1, &getInnerSize};
    methodMap_["getScrollPosition"] = {1, &getScrollPosition};
    methodMap_["getScrollSize"] = {1, &getScrollSize};
    methodMap_["getTagName"] = {1, &getTagName};
    methodMap_["getTextContent"] = {1, &getTextContent};
    methodMap_["hasPointerCapture"] = {2, &hasPointerCapture};
    methodMap_["releasePointerCapture"] = {2, &releasePointerCapture};
    methodMap_["setPointerCapture"] = {2, &setPointerCapture};
    methodMap_["getOffset"] = {1, &getOffset};
    methodMap_["linkRootNode"] = {2, &linkRootNode};
    methodMap_["measure"] = {2, &measure};
    methodMap_["measureInWindow"] = {2, &measureInWindow};
    methodMap_["measureLayout"] = {4, &measureLayout};
    methodMap_["setNativeProps"] = {2, &setNativeProps};
  }

 private:
  static jsi::Value compareDocumentPosition(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 2) {
      return 0;
    }
    auto current = currentRevisionForReference(runtime, args[0]);
    if (current == nullptr) {
      return static_cast<double>(react::dom::DOCUMENT_POSITION_DISCONNECTED);
    }
    std::shared_ptr<const react::ShadowNode> shadowNode;
    std::shared_ptr<const react::ShadowNode> otherShadowNode;
    if (args[0].isNumber() || args[1].isNumber()) {
      if (args[0].isNumber() && args[1].isNumber()) {
        return static_cast<double>(react::dom::DOCUMENT_POSITION_DISCONNECTED);
      }
      if (args[0].isNumber()) {
        auto surfaceId = args[0].getNumber();
        shadowNode = current;
        otherShadowNode = shadowNodeFromJs(runtime, args[1]);
        if (isRootShadowNode(*otherShadowNode)) {
          return static_cast<double>(
              (surfaceId == otherShadowNode->getSurfaceId())
                  ? react::dom::DOCUMENT_POSITION_CONTAINED_BY |
                      react::dom::DOCUMENT_POSITION_FOLLOWING
                  : react::dom::DOCUMENT_POSITION_DISCONNECTED);
        }
      } else {
        auto otherSurfaceId = args[1].getNumber();
        shadowNode = shadowNodeFromJs(runtime, args[0]);
        otherShadowNode = currentRevision(
            runtime, static_cast<react::SurfaceId>(otherSurfaceId));
        if (otherShadowNode == nullptr) {
          return static_cast<double>(
              react::dom::DOCUMENT_POSITION_DISCONNECTED);
        }
        if (isRootShadowNode(*shadowNode)) {
          return static_cast<double>(
              (otherSurfaceId == shadowNode->getSurfaceId())
                  ? react::dom::DOCUMENT_POSITION_CONTAINS |
                      react::dom::DOCUMENT_POSITION_PRECEDING
                  : react::dom::DOCUMENT_POSITION_DISCONNECTED);
        }
      }
    } else {
      shadowNode = shadowNodeFromJs(runtime, args[0]);
      otherShadowNode = shadowNodeFromJs(runtime, args[1]);
    }
    return static_cast<double>(react::dom::compareDocumentPosition(
        current, *shadowNode, *otherShadowNode));
  }

  static jsi::Value getChildNodes(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 1) {
      return jsi::Array(runtime, 0);
    }
    auto current = currentRevisionForReference(runtime, args[0]);
    if (current == nullptr) {
      return jsi::Array(runtime, 0);
    }
    if (args[0].isNumber()) {
      return toJsArray(
          runtime, instanceHandlesFromNodes(runtime, {current}));
    }
    auto children = react::dom::getChildNodes(
        current, *shadowNodeFromJs(runtime, args[0]));
    return toJsArray(
        runtime, instanceHandlesFromNodes(runtime, children));
  }

  static jsi::Value getElementById(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 2 || !args[0].isNumber() || !args[1].isString()) {
      return jsi::Value::undefined();
    }
    auto current = currentRevision(
        runtime, static_cast<react::SurfaceId>(args[0].getNumber()));
    if (current == nullptr) {
      return jsi::Value::undefined();
    }
    auto element = react::dom::getElementById(
        current, args[1].getString(runtime).utf8(runtime));
    if (element == nullptr) {
      return jsi::Value::undefined();
    }
    return element->getInstanceHandle(runtime);
  }

  static jsi::Value getParentNode(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || args[0].isNumber()) {
      return jsi::Value::undefined();
    }
    auto shadowNode = shadowNodeFromJs(runtime, args[0]);
    auto current = currentRevision(runtime, shadowNode->getSurfaceId());
    if (current == nullptr) {
      return jsi::Value::undefined();
    }
    if (react::ShadowNode::sameFamily(*current, *shadowNode)) {
      return jsi::Value{static_cast<double>(shadowNode->getSurfaceId())};
    }
    auto parent = react::dom::getParentNode(current, *shadowNode);
    if (parent == nullptr) {
      return jsi::Value::undefined();
    }
    return parent->getInstanceHandle(runtime);
  }

  static jsi::Value isConnected(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 1) {
      return false;
    }
    auto current = currentRevisionForReference(runtime, args[0]);
    if (current == nullptr) {
      return false;
    }
    if (args[0].isNumber()) {
      return true;
    }
    return react::dom::isConnected(
        current, *shadowNodeFromJs(runtime, args[0]));
  }

  static jsi::Value getBorderWidth(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 1) {
      return numberArray(runtime, {0, 0, 0, 0});
    }
    auto shadowNode = shadowNodeFromJs(runtime, args[0]);
    auto current = currentRevision(runtime, shadowNode->getSurfaceId());
    if (current == nullptr) {
      return numberArray(runtime, {0, 0, 0, 0});
    }
    auto border = react::dom::getBorderWidth(current, *shadowNode);
    return numberArray(
        runtime,
        {static_cast<double>(border.top),
         static_cast<double>(border.right),
         static_cast<double>(border.bottom),
         static_cast<double>(border.left)});
  }

  static jsi::Value getBoundingClientRect(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 1) {
      return numberArray(runtime, {0, 0, 0, 0});
    }
    auto shadowNode = shadowNodeFromJs(runtime, args[0]);
    auto current = currentRevision(runtime, shadowNode->getSurfaceId());
    if (current == nullptr) {
      return numberArray(runtime, {0, 0, 0, 0});
    }
    const auto includeTransform =
        count > 1 && args[1].isBool() && args[1].getBool();
    auto rect = react::dom::getBoundingClientRect(
        current, *shadowNode, includeTransform);
    return numberArray(runtime, {rect.x, rect.y, rect.width, rect.height});
  }

  static jsi::Value getInnerSize(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 1) {
      return numberArray(runtime, {0, 0});
    }
    auto shadowNode = shadowNodeFromJs(runtime, args[0]);
    auto current = currentRevision(runtime, shadowNode->getSurfaceId());
    if (current == nullptr) {
      return numberArray(runtime, {0, 0});
    }
    auto size = react::dom::getInnerSize(current, *shadowNode);
    return numberArray(
        runtime,
        {static_cast<double>(size.width), static_cast<double>(size.height)});
  }

  static jsi::Value getScrollPosition(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 1) {
      return numberArray(runtime, {0, 0});
    }
    auto shadowNode = shadowNodeFromJs(runtime, args[0]);
    auto current = currentRevision(runtime, shadowNode->getSurfaceId());
    if (current == nullptr) {
      return numberArray(runtime, {0, 0});
    }
    auto point = react::dom::getScrollPosition(current, *shadowNode);
    return numberArray(runtime, {point.x, point.y});
  }

  static jsi::Value getScrollSize(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 1) {
      return numberArray(runtime, {0, 0});
    }
    auto shadowNode = shadowNodeFromJs(runtime, args[0]);
    auto current = currentRevision(runtime, shadowNode->getSurfaceId());
    if (current == nullptr) {
      return numberArray(runtime, {0, 0});
    }
    auto size = react::dom::getScrollSize(current, *shadowNode);
    return numberArray(
        runtime,
        {static_cast<double>(size.width), static_cast<double>(size.height)});
  }

  static jsi::Value getTagName(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 1) {
      return jsi::String::createFromAscii(runtime, "");
    }
    return jsi::String::createFromUtf8(
        runtime, react::dom::getTagName(*shadowNodeFromJs(runtime, args[0])));
  }

  static jsi::Value getTextContent(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 1) {
      return jsi::String::createFromAscii(runtime, "");
    }
    if (args[0].isNumber()) {
      auto current = currentRevisionForReference(runtime, args[0]);
      if (current == nullptr) {
        return jsi::String::createFromAscii(runtime, "");
      }
      return jsi::String::createFromUtf8(
          runtime, react::dom::getTextContent(current, *current));
    }
    auto shadowNode = shadowNodeFromJs(runtime, args[0]);
    auto current = currentRevision(runtime, shadowNode->getSurfaceId());
    if (current == nullptr) {
      return jsi::String::createFromAscii(runtime, "");
    }
    return jsi::String::createFromUtf8(
        runtime, react::dom::getTextContent(current, *shadowNode));
  }

  static jsi::Value hasPointerCapture(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 2 || !args[1].isNumber()) {
      return false;
    }
    auto shadowNode = shadowNodeFromJs(runtime, args[0]);
    return bindingOf(runtime).getPointerEventsProcessor().hasPointerCapture(
        static_cast<react::PointerIdentifier>(args[1].getNumber()),
        shadowNode.get());
  }

  static jsi::Value releasePointerCapture(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count >= 2 && args[1].isNumber()) {
      auto shadowNode = shadowNodeFromJs(runtime, args[0]);
      bindingOf(runtime).getPointerEventsProcessor().releasePointerCapture(
          static_cast<react::PointerIdentifier>(args[1].getNumber()),
          shadowNode.get());
    }
    return jsi::Value::undefined();
  }

  static jsi::Value setPointerCapture(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count >= 2 && args[1].isNumber()) {
      auto shadowNode = shadowNodeFromJs(runtime, args[0]);
      bindingOf(runtime).getPointerEventsProcessor().setPointerCapture(
          static_cast<react::PointerIdentifier>(args[1].getNumber()),
          shadowNode);
    }
    return jsi::Value::undefined();
  }

  static jsi::Value getOffset(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    jsi::Array result(runtime, 3);
    result.setValueAtIndex(runtime, 0, jsi::Value::undefined());
    result.setValueAtIndex(runtime, 1, 0);
    result.setValueAtIndex(runtime, 2, 0);
    if (count < 1) {
      return result;
    }
    auto shadowNode = shadowNodeFromJs(runtime, args[0]);
    auto current = currentRevision(runtime, shadowNode->getSurfaceId());
    if (current == nullptr) {
      return result;
    }
    auto offset = react::dom::getOffset(current, *shadowNode);
    result.setValueAtIndex(
        runtime,
        0,
        offset.offsetParent == nullptr
            ? jsi::Value::undefined()
            : offset.offsetParent->getInstanceHandle(runtime));
    result.setValueAtIndex(runtime, 1, offset.top);
    result.setValueAtIndex(runtime, 2, offset.left);
    return result;
  }

  static jsi::Value linkRootNode(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 2 || !args[0].isNumber()) {
      return jsi::Value::undefined();
    }
    auto surfaceId = static_cast<react::SurfaceId>(args[0].getNumber());
    auto current = currentRevision(runtime, surfaceId);
    if (current == nullptr) {
      return jsi::Value::undefined();
    }
    current->setInstanceHandle(
        std::make_shared<react::InstanceHandle>(runtime, args[1], surfaceId));
    return react::Bridging<std::shared_ptr<const react::ShadowNode>>::toJs(
        runtime, current);
  }

  static jsi::Value measure(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 2) {
      return jsi::Value::undefined();
    }
    auto shadowNode = shadowNodeFromJs(runtime, args[0]);
    auto current = currentRevision(runtime, shadowNode->getSurfaceId());
    if (current == nullptr) {
      callNumbers(runtime, args[1], {0, 0, 0, 0, 0, 0});
      return jsi::Value::undefined();
    }
    auto rect = react::dom::measure(current, *shadowNode);
    callNumbers(
        runtime,
        args[1],
        {rect.x, rect.y, rect.width, rect.height, rect.pageX, rect.pageY});
    return jsi::Value::undefined();
  }

  static jsi::Value measureInWindow(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 2) {
      return jsi::Value::undefined();
    }
    auto shadowNode = shadowNodeFromJs(runtime, args[0]);
    auto current = currentRevision(runtime, shadowNode->getSurfaceId());
    if (current == nullptr) {
      callNumbers(runtime, args[1], {0, 0, 0, 0});
      return jsi::Value::undefined();
    }
    auto rect = react::dom::measureInWindow(current, *shadowNode);
    callNumbers(runtime, args[1], {rect.x, rect.y, rect.width, rect.height});
    return jsi::Value::undefined();
  }

  static jsi::Value measureLayout(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 4) {
      return jsi::Value::undefined();
    }
    auto callIfFunction = [&](const jsi::Value& value,
                              std::initializer_list<double> numbers) {
      if (value.isObject() && value.getObject(runtime).isFunction(runtime)) {
        if (numbers.size() == 0) {
          value.getObject(runtime).getFunction(runtime).call(runtime);
        } else {
          callNumbers(runtime, value, numbers);
        }
      }
    };
    auto shadowNode = shadowNodeFromJs(runtime, args[0]);
    auto relative = shadowNodeFromJs(runtime, args[1]);
    auto current = currentRevision(runtime, shadowNode->getSurfaceId());
    if (current == nullptr) {
      callIfFunction(args[2], {});
      return jsi::Value::undefined();
    }
    auto maybeRect = react::dom::measureLayout(current, *shadowNode, *relative);
    if (!maybeRect) {
      callIfFunction(args[2], {});
      return jsi::Value::undefined();
    }
    callIfFunction(
        args[3],
        {maybeRect->x, maybeRect->y, maybeRect->width, maybeRect->height});
    return jsi::Value::undefined();
  }

  static jsi::Value setNativeProps(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count >= 2) {
      auto shadowNode = shadowNodeFromJs(runtime, args[0]);
      bindingOf(runtime).getUIManager().setNativeProps_DEPRECATED(
          shadowNode, react::RawProps(runtime, args[1]));
    }
    return jsi::Value::undefined();
  }
};
} // namespace

std::shared_ptr<react::TurboModule> createHeadlessNativeDOM(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<HeadlessNativeDOMTurboModule>(std::move(jsInvoker));
}
