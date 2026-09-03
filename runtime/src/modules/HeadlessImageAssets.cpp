#include "HeadlessImageAssets.h"
#include "ImagePixelSize.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

namespace {
std::filesystem::path existingRegularFile(const std::filesystem::path& candidate) {
  std::error_code error;
  if (!candidate.empty() && std::filesystem::is_regular_file(candidate, error)) {
    return candidate;
  }
  return {};
}

std::string androidResourceIdentifier(std::string path) {
  if (const auto query = path.find('?'); query != std::string::npos) {
    path.resize(query);
  }
  constexpr std::string_view marker = "/assets/";
  if (const auto at = path.find(marker); at != std::string::npos) {
    path = path.substr(at + marker.size());
  } else if (const auto slash = path.find_last_of("/\\");
             slash != std::string::npos) {
    path = path.substr(slash + 1);
  }
  if (const auto dot = path.rfind('.');
      dot != std::string::npos && dot > path.find_last_of("/\\")) {
    path.resize(dot);
  }
  std::string identifier;
  identifier.reserve(path.size());
  for (const unsigned char character : path) {
    if (character == '/' || character == '\\') {
      identifier.push_back('_');
    } else {
      const auto lowered = static_cast<char>(std::tolower(character));
      if ((lowered >= 'a' && lowered <= 'z') ||
          (lowered >= '0' && lowered <= '9') || lowered == '_') {
        identifier.push_back(lowered);
      }
    }
  }
  constexpr std::string_view assetsPrefix = "assets_";
  if (identifier.starts_with(assetsPrefix)) {
    identifier.erase(0, assetsPrefix.size());
  }
  return identifier;
}
} // namespace

std::filesystem::path resolveHeadlessLocalImage(
    const std::string& uri,
    const std::filesystem::path& assetRoot) {
  if (uri.empty()) {
    return {};
  }
  auto path = uri;
  if (const auto query = path.find('?'); query != std::string::npos) {
    path.resize(query);
  }
  if (path.starts_with("file://")) {
    if (const auto local = existingRegularFile(path.substr(7));
        !local.empty()) {
      return local;
    }
  } else if (path.find("://") == std::string::npos) {
    if (const auto local = existingRegularFile(path); !local.empty()) {
      return local;
    }
  }
  if (assetRoot.empty()) {
    return {};
  }
  static constexpr const char* folders[] = {
      "drawable",
      "drawable-mdpi",
      "drawable-hdpi",
      "drawable-xhdpi",
      "drawable-xxhdpi",
      "drawable-xxxhdpi",
      "drawable-ldpi",
      "raw",
  };
  for (const auto* folder : folders) {
    const std::string marker = std::string("/") + folder + "/";
    std::string relative;
    if (const auto at = path.find(marker); at != std::string::npos) {
      relative = path.substr(at + 1);
    } else if (path.starts_with(std::string(folder) + "/")) {
      relative = path;
    }
    if (!relative.empty()) {
      if (const auto found = existingRegularFile(assetRoot / relative);
          !found.empty()) {
        return found;
      }
    }
  }
  const auto identifier = androidResourceIdentifier(uri);
  if (identifier.empty()) {
    return {};
  }
  static constexpr const char* extensions[] = {
      ".png", ".jpg", ".jpeg", ".webp", ".gif"};
  for (const auto* folder : folders) {
    for (const auto* extension : extensions) {
      if (const auto found = existingRegularFile(
              assetRoot / folder / (identifier + extension));
          !found.empty()) {
        return found;
      }
    }
  }
  return {};
}

std::optional<std::pair<int, int>> headlessLocalImagePixelSize(
    const std::filesystem::path& path) {
  if (path.empty()) {
    return std::nullopt;
  }
#if defined(__APPLE__)
  const auto utf8 = path.string();
  auto url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault,
      reinterpret_cast<const UInt8*>(utf8.data()),
      static_cast<CFIndex>(utf8.size()),
      false);
  if (url == nullptr) {
    return std::nullopt;
  }
  auto source = CGImageSourceCreateWithURL(url, nullptr);
  CFRelease(url);
  if (source == nullptr) {
    return std::nullopt;
  }
  auto properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
  CFRelease(source);
  if (properties == nullptr) {
    return std::nullopt;
  }
  int width = 0;
  int height = 0;
  auto widthValue = static_cast<CFNumberRef>(
      CFDictionaryGetValue(properties, kCGImagePropertyPixelWidth));
  auto heightValue = static_cast<CFNumberRef>(
      CFDictionaryGetValue(properties, kCGImagePropertyPixelHeight));
  if (widthValue != nullptr) {
    CFNumberGetValue(widthValue, kCFNumberIntType, &width);
  }
  if (heightValue != nullptr) {
    CFNumberGetValue(heightValue, kCFNumberIntType, &height);
  }
  CFRelease(properties);
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }
  return std::pair{width, height};
#else
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  const std::string bytes(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());
  return imagePixelSizeFromBytes(bytes);
#endif
}
