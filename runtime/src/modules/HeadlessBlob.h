#pragma once

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

std::string headlessBlobStore(std::string bytes); // new uuid, store, return id
void headlessBlobStoreWithId(const std::string& blobId, std::string bytes);
std::optional<std::string> headlessBlobResolve(
    const std::string& blobId, int offset, int size); // size<0 means rest
void headlessBlobRelease(const std::string& blobId);
void headlessBlobReset(); // clear store + handler flags; for tests/shutdown

void headlessBlobSetNetworkingHandler(bool enabled);
bool headlessBlobNetworkingHandlerEnabled();
void headlessBlobAddWebSocketHandler(int socketId);
void headlessBlobRemoveWebSocketHandler(int socketId);
bool headlessBlobWebSocketHandlerEnabled(int socketId);

std::string headlessBlobBase64Encode(std::string_view bytes);
std::string headlessBlobBase64Decode(std::string_view base64); // empty on error

std::shared_ptr<facebook::react::TurboModule> createHeadlessBlobModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
std::shared_ptr<facebook::react::TurboModule> createHeadlessFileReaderModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
