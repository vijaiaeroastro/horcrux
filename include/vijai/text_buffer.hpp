#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vijai {

enum class LineEnding { lf, crlf, cr };

struct TextChange {
  std::size_t byte_offset;
  std::size_t removed_bytes;
  std::size_t inserted_bytes;
};

class TextBuffer {
 public:
  static TextBuffer from_text(std::string text);

  [[nodiscard]] const std::string& text() const noexcept;
  [[nodiscard]] LineEnding line_ending() const noexcept;
  [[nodiscard]] std::size_t line_count() const noexcept;
  [[nodiscard]] bool can_undo() const noexcept;
  [[nodiscard]] bool can_redo() const noexcept;
  [[nodiscard]] std::optional<std::size_t> find(std::string_view query,
                                                std::size_t start_offset,
                                                bool wrap = true) const;

  void insert(std::size_t byte_offset, std::string_view inserted_text);
  void erase(std::size_t byte_offset, std::size_t byte_count);
  std::optional<TextChange> undo();
  std::optional<TextChange> redo();

 private:
  explicit TextBuffer(std::string text, LineEnding line_ending);

  struct Edit {
    enum class Kind { insert, erase };
    Kind kind;
    std::size_t byte_offset;
    std::string text;
  };

  void apply(const Edit& edit);

  std::string text_;
  LineEnding line_ending_;
  std::vector<Edit> undo_stack_;
  std::vector<Edit> redo_stack_;
};

}  // namespace vijai
