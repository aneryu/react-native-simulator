#pragma once

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <memory>

std::shared_ptr<facebook::react::TurboModule> createHeadlessAlertManagerModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
std::shared_ptr<facebook::react::TurboModule> createHeadlessActionSheetManagerModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
std::shared_ptr<facebook::react::TurboModule> createHeadlessSettingsManagerModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
std::shared_ptr<facebook::react::TurboModule> createHeadlessStatusBarManagerIOS(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
std::shared_ptr<facebook::react::TurboModule> createHeadlessPushNotificationManagerModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
