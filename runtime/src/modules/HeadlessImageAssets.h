#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

std::filesystem::path resolveHeadlessLocalImage(
    const std::string& uri,
    const std::filesystem::path& assetRoot);

std::optional<std::pair<int, int>> headlessLocalImagePixelSize(
    const std::filesystem::path& path);
