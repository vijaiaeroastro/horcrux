#pragma once

#include "vijai/text_buffer.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace vijai {

enum class TextEncoding { utf8, utf8_bom, utf16_le, utf16_be };

class Document {
 public:
  static std::optional<Document> open(const std::filesystem::path& path, std::string& error);
  static Document create(const std::filesystem::path& path);
  static Document untitled();

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] bool has_path() const noexcept;
  [[nodiscard]] bool is_dirty() const noexcept;
  [[nodiscard]] TextEncoding encoding() const noexcept;
  [[nodiscard]] const TextBuffer& buffer() const noexcept;
  [[nodiscard]] TextBuffer& buffer() noexcept;

  bool save(std::string& error);
  bool save_as(const std::filesystem::path& path, std::string& error);
  void restore_text(std::string text);
  void mark_clean() noexcept;

 private:
  Document(std::filesystem::path path, TextBuffer buffer, TextEncoding encoding);

  std::filesystem::path path_;
  TextBuffer buffer_;
  TextEncoding encoding_;
  std::string clean_text_;
};

}  // namespace vijai
