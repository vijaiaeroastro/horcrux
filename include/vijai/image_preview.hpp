#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vijai {

struct RgbaImage {
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::vector<std::uint8_t> pixels;
};

[[nodiscard]] bool is_previewable_image(const std::filesystem::path& path);
[[nodiscard]] std::optional<RgbaImage> load_image_preview(const std::filesystem::path& path,
                                                           std::string& error);

}  // namespace vijai
