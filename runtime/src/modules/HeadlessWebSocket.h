#pragma once

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>

#include <memory>
#include <string>

void headlessWebSocketSendBinary(int socketId, const std::string& bytes);

std::shared_ptr<facebook::react::TurboModule> createHeadlessWebSocketModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

void headlessWebSocketReset(); // close all sockets
