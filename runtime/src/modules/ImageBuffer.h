#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct RgbaImage {
  int width{0};
  int height{0};
  std::vector<std::uint8_t> rgba;
};

std::optional<RgbaImage> decodePngToRgba(std::string_view bytes);
std::optional<std::string> encodeRgbaToPng(const RgbaImage& image);
RgbaImage cropRgbaImage(
    const RgbaImage& image,
    int x,
    int y,
    int width,
    int height);
RgbaImage resizeRgbaImage(
    const RgbaImage& image,
    int destWidth,
    int destHeight,
    const std::string& resizeMode);
