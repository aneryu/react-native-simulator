#include "ImagePixelSize.h"

#include <cstdint>

namespace {
constexpr unsigned char kPngSignature[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

std::uint32_t readBe32(std::string_view bytes, std::size_t offset) {
  return (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset])) << 24) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 16) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 8) |
      static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 3]));
}

std::uint16_t readBe16(std::string_view bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(
      (static_cast<unsigned char>(bytes[offset]) << 8) |
      static_cast<unsigned char>(bytes[offset + 1]));
}

std::optional<std::pair<int, int>> pngSize(std::string_view bytes) {
  if (bytes.size() < 24) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < sizeof(kPngSignature); ++i) {
    if (static_cast<unsigned char>(bytes[i]) != kPngSignature[i]) {
      return std::nullopt;
    }
  }
  const int width = static_cast<int>(readBe32(bytes, 16));
  const int height = static_cast<int>(readBe32(bytes, 20));
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }
  return std::pair{width, height};
}

std::optional<std::pair<int, int>> jpegSize(std::string_view bytes) {
  if (bytes.size() < 4 || static_cast<unsigned char>(bytes[0]) != 0xff ||
      static_cast<unsigned char>(bytes[1]) != 0xd8) {
    return std::nullopt;
  }
  std::size_t offset = 2;
  while (offset + 9 < bytes.size()) {
    if (static_cast<unsigned char>(bytes[offset]) != 0xff) {
      ++offset;
      continue;
    }
    const auto marker = static_cast<unsigned char>(bytes[offset + 1]);
    offset += 2;
    if (marker == 0xd8 || marker == 0xd9 || (marker >= 0xd0 && marker <= 0xd7)) {
      continue;
    }
    if (offset + 2 > bytes.size()) {
      break;
    }
    const auto length = readBe16(bytes, offset);
    if (length < 2 || offset + length > bytes.size()) {
      break;
    }
    if ((marker >= 0xc0 && marker <= 0xc3) ||
        (marker >= 0xc5 && marker <= 0xc7) ||
        (marker >= 0xc9 && marker <= 0xcb) ||
        (marker >= 0xcd && marker <= 0xcf)) {
      if (length < 7) {
        break;
      }
      const int height = readBe16(bytes, offset + 3);
      const int width = readBe16(bytes, offset + 5);
      if (width <= 0 || height <= 0) {
        return std::nullopt;
      }
      return std::pair{width, height};
    }
    offset += length;
  }
  return std::nullopt;
}

std::optional<std::pair<int, int>> gifSize(std::string_view bytes) {
  if (bytes.size() < 10 ||
      !(bytes.starts_with("GIF87a") || bytes.starts_with("GIF89a"))) {
    return std::nullopt;
  }
  const int width = static_cast<unsigned char>(bytes[6]) |
      (static_cast<unsigned char>(bytes[7]) << 8);
  const int height = static_cast<unsigned char>(bytes[8]) |
      (static_cast<unsigned char>(bytes[9]) << 8);
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }
  return std::pair{width, height};
}

std::optional<std::pair<int, int>> webpSize(std::string_view bytes) {
  if (bytes.size() < 30 || !bytes.starts_with("RIFF") ||
      bytes.substr(8, 4) != "WEBP") {
    return std::nullopt;
  }
  const auto fourcc = bytes.substr(12, 4);
  if (fourcc == "VP8X" && bytes.size() >= 30) {
    const int width = 1 + (static_cast<unsigned char>(bytes[24]) |
        (static_cast<unsigned char>(bytes[25]) << 8) |
        (static_cast<unsigned char>(bytes[26]) << 16));
    const int height = 1 + (static_cast<unsigned char>(bytes[27]) |
        (static_cast<unsigned char>(bytes[28]) << 8) |
        (static_cast<unsigned char>(bytes[29]) << 16));
    if (width <= 0 || height <= 0) {
      return std::nullopt;
    }
    return std::pair{width, height};
  }
  if (fourcc == "VP8 " && bytes.size() >= 30) {
    const int width = readBe16(bytes, 26) & 0x3fff;
    const int height = readBe16(bytes, 28) & 0x3fff;
    if (width <= 0 || height <= 0) {
      return std::nullopt;
    }
    return std::pair{width, height};
  }
  if (fourcc == "VP8L" && bytes.size() >= 25) {
    const auto bits = static_cast<unsigned char>(bytes[21]) |
        (static_cast<unsigned>(static_cast<unsigned char>(bytes[22])) << 8) |
        (static_cast<unsigned>(static_cast<unsigned char>(bytes[23])) << 16) |
        (static_cast<unsigned>(static_cast<unsigned char>(bytes[24])) << 24);
    const int width = (bits & 0x3fff) + 1;
    const int height = ((bits >> 14) & 0x3fff) + 1;
    return std::pair{width, height};
  }
  return std::nullopt;
}
} // namespace

bool bytesLookLikePng(std::string_view bytes) {
  return pngSize(bytes).has_value();
}

bool bytesLookLikeJpeg(std::string_view bytes) {
  return bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff &&
      static_cast<unsigned char>(bytes[1]) == 0xd8;
}

std::string guessImageExtension(std::string_view bytes) {
  if (bytesLookLikeJpeg(bytes)) {
    return "jpeg";
  }
  if (bytesLookLikePng(bytes)) {
    return "png";
  }
  if (gifSize(bytes)) {
    return "gif";
  }
  if (webpSize(bytes)) {
    return "webp";
  }
  return "png";
}

std::optional<std::pair<int, int>> imagePixelSizeFromBytes(std::string_view bytes) {
  if (auto size = pngSize(bytes)) {
    return size;
  }
  if (auto size = jpegSize(bytes)) {
    return size;
  }
  if (auto size = gifSize(bytes)) {
    return size;
  }
  return webpSize(bytes);
}
