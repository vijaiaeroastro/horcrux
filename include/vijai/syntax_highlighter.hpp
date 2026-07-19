#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vijai {

enum class SyntaxKind { plain, keyword, type, string, comment, number, preprocessor };

struct SyntaxSpan {
  std::size_t start{0};
  std::size_t length{0};
  SyntaxKind kind{SyntaxKind::plain};
};

// Tree-sitter-backed highlighting for C++. Other languages retain a small
// lexical fallback until their official grammars are added as submodules.
class SyntaxHighlighter {
 public:
  explicit SyntaxHighlighter(std::filesystem::path path = {});
  ~SyntaxHighlighter();

  SyntaxHighlighter(const SyntaxHighlighter&) = delete;
  SyntaxHighlighter& operator=(const SyntaxHighlighter&) = delete;
  SyntaxHighlighter(SyntaxHighlighter&&) noexcept;
  SyntaxHighlighter& operator=(SyntaxHighlighter&&) noexcept;

  void set_path(const std::filesystem::path& path);
  void set_source(std::string_view source);
  [[nodiscard]] std::vector<SyntaxSpan> highlight_line(std::string_view line,
                                                        std::size_t source_offset = 0U) const;

 public:
  enum class Language {
    plain, cpp, c_family, python, go, javascript, json, markdown, yaml, toml, typst, tex, bibtex
  };

 private:
  [[nodiscard]] bool is_keyword(std::string_view word) const;
  [[nodiscard]] bool supports_hash_comments() const;
  [[nodiscard]] bool supports_slash_comments() const;

  Language language_{Language::plain};
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace vijai
