#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

std::optional<std::pair<int, int>> imagePixelSizeFromBytes(std::string_view bytes);
bool bytesLookLikePng(std::string_view bytes);
bool bytesLookLikeJpeg(std::string_view bytes);
std::string guessImageExtension(std::string_view bytes);
