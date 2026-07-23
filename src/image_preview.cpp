#include "vijai/image_preview.hpp"

#include <algorithm>
#include <cctype>

#include <png.h>

namespace vijai {

bool is_previewable_image(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return extension == ".png";
}

std::optional<RgbaImage> load_image_preview(const std::filesystem::path& path, std::string& error) {
  if (!is_previewable_image(path)) {
    error = "Image preview supports PNG files";
    return std::nullopt;
  }

  png_image decoded{};
  decoded.version = PNG_IMAGE_VERSION;
  if (png_image_begin_read_from_file(&decoded, path.string().c_str()) == 0) {
    error = decoded.message;
    return std::nullopt;
  }
  decoded.format = PNG_FORMAT_RGBA;
  RgbaImage image{.width = decoded.width, .height = decoded.height,
                  .pixels = std::vector<std::uint8_t>(PNG_IMAGE_SIZE(decoded))};
  if (png_image_finish_read(&decoded, nullptr, image.pixels.data(), 0, nullptr) == 0) {
    error = decoded.message;
    png_image_free(&decoded);
    return std::nullopt;
  }
  png_image_free(&decoded);
  return image;
}

}  // namespace vijai
