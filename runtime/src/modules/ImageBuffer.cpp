#include "ImageBuffer.h"

#include <png.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
struct PngMemory {
  const unsigned char* data{nullptr};
  std::size_t size{0};
  std::size_t offset{0};
};

void pngRead(png_structp png, png_bytep out, png_size_t count) {
  auto* memory = static_cast<PngMemory*>(png_get_io_ptr(png));
  if (memory->offset + count > memory->size) {
    png_error(png, "PNG read past end");
  }
  std::memcpy(out, memory->data + memory->offset, count);
  memory->offset += count;
}

void pngWrite(png_structp png, png_bytep data, png_size_t count) {
  auto* output = static_cast<std::string*>(png_get_io_ptr(png));
  output->append(reinterpret_cast<const char*>(data), count);
}

void pngFlush(png_structp) {}

std::uint8_t sampleBilinear(
    const RgbaImage& image,
    double x,
    double y,
    int channel) {
  const double clampedX = std::clamp(x, 0.0, static_cast<double>(image.width - 1));
  const double clampedY = std::clamp(y, 0.0, static_cast<double>(image.height - 1));
  const int x0 = static_cast<int>(std::floor(clampedX));
  const int y0 = static_cast<int>(std::floor(clampedY));
  const int x1 = std::min(image.width - 1, x0 + 1);
  const int y1 = std::min(image.height - 1, y0 + 1);
  const double fx = clampedX - x0;
  const double fy = clampedY - y0;
  const auto at = [&](int px, int py) {
    return image.rgba[(static_cast<std::size_t>(py) * image.width + px) * 4 +
                      static_cast<std::size_t>(channel)];
  };
  const double top = at(x0, y0) * (1.0 - fx) + at(x1, y0) * fx;
  const double bottom = at(x0, y1) * (1.0 - fx) + at(x1, y1) * fx;
  return static_cast<std::uint8_t>(std::lround(top * (1.0 - fy) + bottom * fy));
}

RgbaImage stretchImage(const RgbaImage& image, int destWidth, int destHeight) {
  RgbaImage output;
  output.width = destWidth;
  output.height = destHeight;
  output.rgba.resize(static_cast<std::size_t>(destWidth) * destHeight * 4);
  const double scaleX = static_cast<double>(image.width) / destWidth;
  const double scaleY = static_cast<double>(image.height) / destHeight;
  for (int y = 0; y < destHeight; ++y) {
    for (int x = 0; x < destWidth; ++x) {
      const double srcX = (x + 0.5) * scaleX - 0.5;
      const double srcY = (y + 0.5) * scaleY - 0.5;
      const std::size_t index =
          (static_cast<std::size_t>(y) * destWidth + x) * 4;
      for (int channel = 0; channel < 4; ++channel) {
        output.rgba[index + channel] =
            sampleBilinear(image, srcX, srcY, channel);
      }
    }
  }
  return output;
}
} // namespace

std::optional<RgbaImage> decodePngToRgba(std::string_view bytes) {
  if (bytes.size() < 8 || png_sig_cmp(
          reinterpret_cast<png_const_bytep>(bytes.data()), 0, 8) != 0) {
    return std::nullopt;
  }
  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (png == nullptr) {
    return std::nullopt;
  }
  png_infop info = png_create_info_struct(png);
  if (info == nullptr) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    return std::nullopt;
  }
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, nullptr);
    return std::nullopt;
  }
  PngMemory memory{
      reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), 0};
  png_set_read_fn(png, &memory, pngRead);
  png_read_info(png, info);
  const int width = static_cast<int>(png_get_image_width(png, info));
  const int height = static_cast<int>(png_get_image_height(png, info));
  const auto colorType = png_get_color_type(png, info);
  const auto bitDepth = png_get_bit_depth(png, info);
  if (bitDepth == 16) {
    png_set_strip_16(png);
  }
  if (colorType == PNG_COLOR_TYPE_PALETTE) {
    png_set_palette_to_rgb(png);
  }
  if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) {
    png_set_expand_gray_1_2_4_to_8(png);
  }
  if (png_get_valid(png, info, PNG_INFO_tRNS)) {
    png_set_tRNS_to_alpha(png);
  }
  if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY ||
      colorType == PNG_COLOR_TYPE_PALETTE) {
    png_set_filler(png, 0xff, PNG_FILLER_AFTER);
  }
  if (colorType == PNG_COLOR_TYPE_GRAY ||
      colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
    png_set_gray_to_rgb(png);
  }
  png_read_update_info(png, info);
  RgbaImage image;
  image.width = width;
  image.height = height;
  image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
  std::vector<png_bytep> rows(static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    rows[static_cast<std::size_t>(y)] =
        image.rgba.data() + static_cast<std::size_t>(y) * width * 4;
  }
  png_read_image(png, rows.data());
  png_destroy_read_struct(&png, &info, nullptr);
  return image;
}

std::optional<std::string> encodeRgbaToPng(const RgbaImage& image) {
  if (image.width <= 0 || image.height <= 0 ||
      image.rgba.size() !=
          static_cast<std::size_t>(image.width) * image.height * 4) {
    return std::nullopt;
  }
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (png == nullptr) {
    return std::nullopt;
  }
  png_infop info = png_create_info_struct(png);
  if (info == nullptr) {
    png_destroy_write_struct(&png, nullptr);
    return std::nullopt;
  }
  std::string encoded;
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    return std::nullopt;
  }
  png_set_write_fn(png, &encoded, pngWrite, pngFlush);
  png_set_IHDR(
      png,
      info,
      static_cast<png_uint_32>(image.width),
      static_cast<png_uint_32>(image.height),
      8,
      PNG_COLOR_TYPE_RGBA,
      PNG_INTERLACE_NONE,
      PNG_COMPRESSION_TYPE_DEFAULT,
      PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  std::vector<png_bytep> rows(static_cast<std::size_t>(image.height));
  for (int y = 0; y < image.height; ++y) {
    rows[static_cast<std::size_t>(y)] = const_cast<png_bytep>(
        image.rgba.data() + static_cast<std::size_t>(y) * image.width * 4);
  }
  png_write_image(png, rows.data());
  png_write_end(png, nullptr);
  png_destroy_write_struct(&png, &info);
  return encoded;
}

RgbaImage cropRgbaImage(
    const RgbaImage& image,
    int x,
    int y,
    int width,
    int height) {
  RgbaImage output;
  output.width = width;
  output.height = height;
  output.rgba.resize(static_cast<std::size_t>(width) * height * 4);
  for (int row = 0; row < height; ++row) {
    const auto* src = image.rgba.data() +
        (static_cast<std::size_t>(y + row) * image.width + x) * 4;
    auto* dst = output.rgba.data() + static_cast<std::size_t>(row) * width * 4;
    std::memcpy(dst, src, static_cast<std::size_t>(width) * 4);
  }
  return output;
}

RgbaImage resizeRgbaImage(
    const RgbaImage& image,
    int destWidth,
    int destHeight,
    const std::string& resizeMode) {
  if (destWidth <= 0 || destHeight <= 0) {
    return {};
  }
  if (resizeMode == "center" && image.width <= destWidth &&
      image.height <= destHeight) {
    return image;
  }
  if (resizeMode == "cover") {
    const double scale = std::max(
        static_cast<double>(destWidth) / image.width,
        static_cast<double>(destHeight) / image.height);
    const int cropWidth = std::max(
        1, static_cast<int>(std::lround(destWidth / scale)));
    const int cropHeight = std::max(
        1, static_cast<int>(std::lround(destHeight / scale)));
    const int cropX = std::max(0, (image.width - cropWidth) / 2);
    const int cropY = std::max(0, (image.height - cropHeight) / 2);
    auto cropped = cropRgbaImage(
        image,
        cropX,
        cropY,
        std::min(cropWidth, image.width - cropX),
        std::min(cropHeight, image.height - cropY));
    return stretchImage(cropped, destWidth, destHeight);
  }
  if (resizeMode == "stretch" || resizeMode == "repeat") {
    return stretchImage(image, destWidth, destHeight);
  }
  const double scale = std::min(
      static_cast<double>(destWidth) / image.width,
      static_cast<double>(destHeight) / image.height);
  const int outWidth =
      std::max(1, static_cast<int>(std::lround(image.width * scale)));
  const int outHeight =
      std::max(1, static_cast<int>(std::lround(image.height * scale)));
  return stretchImage(image, outWidth, outHeight);
}
