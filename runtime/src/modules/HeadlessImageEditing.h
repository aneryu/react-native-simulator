#pragma once
#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <filesystem>
#include <memory>
#include <string>

std::shared_ptr<facebook::react::TurboModule> createHeadlessImageEditingModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
    std::filesystem::path assetDirectory);

std::shared_ptr<facebook::react::TurboModule> createHeadlessImageStoreModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
    std::filesystem::path assetDirectory);
