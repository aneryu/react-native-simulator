#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct HeadlessHttpRequest {
  int requestId{0};
  std::string method{"GET"};
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  double timeoutMs{0};
};

struct HeadlessHttpResponse {
  int status{0};
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  std::string error;
  bool timeout{false};
};

void headlessHttpSend(
    HeadlessHttpRequest request,
    std::function<void(HeadlessHttpResponse)> callback);
void headlessHttpAbort(int requestId);

void headlessCacheImageBytes(const std::string& uri, const std::string& bytes);
std::filesystem::path headlessCachedImagePath(const std::string& uri);
std::optional<std::pair<int, int>> headlessImagePixelSizeFromBytes(
    const std::string& bytes);
void headlessFetchImage(
    const std::string& uri,
    std::function<void(std::filesystem::path, std::string)> onDone);
void headlessPrefetchImage(
    const std::string& uri,
    std::function<void()> onDone);
void headlessImageRequestsReset();

std::string headlessClipboardGet();
void headlessClipboardSet(const std::string& contents);
