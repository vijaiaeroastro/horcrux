#include "horcrux/text_buffer.hpp"

#include <algorithm>
#include <stdexcept>

namespace horcrux {
namespace {

LineEnding detect_line_ending(const std::string& text) {
  const auto crlf = text.find("\r\n");
  if (crlf != std::string::npos) {
    return LineEnding::crlf;
  }
  if (text.find('\r') != std::string::npos) {
    return LineEnding::cr;
  }
  return LineEnding::lf;
}

}  // namespace

TextBuffer::TextBuffer(std::string text, const LineEnding line_ending)
    : text_(std::move(text)), line_ending_(line_ending) {}

TextBuffer TextBuffer::from_text(std::string text) {
  const auto line_ending = detect_line_ending(text);
  return TextBuffer(std::move(text), line_ending);
}

const std::string& TextBuffer::text() const noexcept { return text_; }

LineEnding TextBuffer::line_ending() const noexcept { return line_ending_; }

std::size_t TextBuffer::line_count() const noexcept {
  std::size_t lines = 1;
  for (std::size_t index = 0; index < text_.size(); ++index) {
    if (text_[index] == '\n') {
      ++lines;
    } else if (text_[index] == '\r' &&
               (index + 1 == text_.size() || text_[index + 1] != '\n')) {
      ++lines;
    }
  }
  return lines;
}

bool TextBuffer::can_undo() const noexcept { return !undo_stack_.empty(); }

bool TextBuffer::can_redo() const noexcept { return !redo_stack_.empty(); }

std::optional<std::size_t> TextBuffer::find(const std::string_view query,
                                            const std::size_t start_offset,
                                            const bool wrap) const {
  if (query.empty()) return std::nullopt;
  const auto bounded_start = std::min(start_offset, text_.size());
  auto match = text_.find(query, bounded_start);
  if (match == std::string::npos && wrap && bounded_start > 0U) {
    match = text_.find(query, 0U);
    if (match >= bounded_start) match = std::string::npos;
  }
  return match == std::string::npos ? std::nullopt : std::optional<std::size_t>(match);
}

void TextBuffer::insert(const std::size_t byte_offset, const std::string_view inserted_text) {
  if (byte_offset > text_.size()) {
    throw std::out_of_range("text insertion offset exceeds buffer size");
  }
  const Edit edit{.kind = Edit::Kind::insert, .byte_offset = byte_offset,
                  .text = std::string(inserted_text)};
  apply(edit);
  undo_stack_.push_back(edit);
  redo_stack_.clear();
}

void TextBuffer::erase(const std::size_t byte_offset, const std::size_t byte_count) {
  if (byte_offset > text_.size() || byte_count > text_.size() - byte_offset) {
    throw std::out_of_range("text erase range exceeds buffer size");
  }
  Edit edit{.kind = Edit::Kind::erase, .byte_offset = byte_offset,
            .text = text_.substr(byte_offset, byte_count)};
  apply(edit);
  undo_stack_.push_back(std::move(edit));
  redo_stack_.clear();
}

std::optional<TextChange> TextBuffer::undo() {
  if (undo_stack_.empty()) {
    return std::nullopt;
  }
  const Edit edit = undo_stack_.back();
  undo_stack_.pop_back();
  const Edit inverse{.kind = edit.kind == Edit::Kind::insert ? Edit::Kind::erase : Edit::Kind::insert,
                     .byte_offset = edit.byte_offset, .text = edit.text};
  apply(inverse);
  redo_stack_.push_back(edit);
  return TextChange{.byte_offset = inverse.byte_offset,
                    .removed_bytes = inverse.kind == Edit::Kind::erase ? inverse.text.size() : 0U,
                    .inserted_bytes = inverse.kind == Edit::Kind::insert ? inverse.text.size() : 0U};
}

std::optional<TextChange> TextBuffer::redo() {
  if (redo_stack_.empty()) {
    return std::nullopt;
  }
  const Edit edit = redo_stack_.back();
  redo_stack_.pop_back();
  apply(edit);
  undo_stack_.push_back(edit);
  return TextChange{.byte_offset = edit.byte_offset,
                    .removed_bytes = edit.kind == Edit::Kind::erase ? edit.text.size() : 0U,
                    .inserted_bytes = edit.kind == Edit::Kind::insert ? edit.text.size() : 0U};
}

void TextBuffer::apply(const Edit& edit) {
  if (edit.kind == Edit::Kind::insert) {
    text_.insert(edit.byte_offset, edit.text);
  } else {
    text_.erase(edit.byte_offset, edit.text.size());
  }
}

}  // namespace horcrux
