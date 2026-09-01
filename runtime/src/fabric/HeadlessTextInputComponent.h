#pragma once

#include <react/renderer/components/text/BaseParagraphComponentDescriptor.h>
#include <react/renderer/components/textinput/BaseTextInputProps.h>
#include <react/renderer/components/textinput/BaseTextInputShadowNode.h>
#include <react/renderer/components/textinput/TextInputEventEmitter.h>
#include <react/renderer/components/textinput/TextInputState.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <iostextinput/TextInputShadowNode.h>

namespace facebook::react {

extern const char HeadlessAndroidTextInputComponentName[];

template <const char* componentName>
using HeadlessTextInputShadowNode = BaseTextInputShadowNode<
    componentName,
    BaseTextInputProps,
    TextInputEventEmitter,
    TextInputState>;

using HeadlessAndroidTextInputShadowNode =
    HeadlessTextInputShadowNode<HeadlessAndroidTextInputComponentName>;

template <typename ShadowNodeT>
class HeadlessTextInputComponentDescriptor final
    : public ConcreteComponentDescriptor<ShadowNodeT> {
 public:
  explicit HeadlessTextInputComponentDescriptor(
      const ComponentDescriptorParameters& parameters)
      : ConcreteComponentDescriptor<ShadowNodeT>(parameters),
        textLayoutManager_(getManagerByName<TextLayoutManager>(
            this->contextContainer_, TextLayoutManagerKey)) {}

 protected:
  void adopt(ShadowNode& shadowNode) const override {
    ConcreteComponentDescriptor<ShadowNodeT>::adopt(shadowNode);
    static_cast<ShadowNodeT&>(shadowNode).setTextLayoutManager(
        textLayoutManager_);
  }

 private:
  std::shared_ptr<const TextLayoutManager> textLayoutManager_;
};

using HeadlessAndroidTextInputComponentDescriptor =
    HeadlessTextInputComponentDescriptor<HeadlessAndroidTextInputShadowNode>;
using HeadlessIOSTextInputComponentDescriptor =
    HeadlessTextInputComponentDescriptor<TextInputShadowNode>;

} // namespace facebook::react
