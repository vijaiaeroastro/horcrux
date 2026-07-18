#include "horcrux/editor_buffer.hpp"

#include "horcrux/session.hpp"

#include <algorithm>
#include <utility>

namespace horcrux {
namespace {

std::string buffer_identity(const Document& document) {
  return recovery_identity(document.has_path() ? document.path() : std::filesystem::path{});
}

}  // namespace

EditorBuffer::EditorBuffer(Document document, const std::filesystem::path& state_directory,
                           const bool restore_session)
    : document_(std::move(document)),
      identity_(buffer_identity(document_)),
      journal_(state_directory, identity_),
      state_directory_(state_directory) {
  std::string error;
  recovery_snapshot_ = journal_.latest_snapshot(error);
  if (!error.empty()) initialization_message_ = error;
  if (!restore_session) return;

  const auto session = load_document_session(state_directory_, identity_, error);
  if (session) {
    cursor_ = std::min(session->cursor_byte, document_.buffer().text().size());
    top_line_ = session->top_line > 0U ? session->top_line - 1U : 0U;
  } else if (!error.empty()) {
    initialization_message_ = error;
  }
}

Document& EditorBuffer::document() noexcept { return document_; }
const Document& EditorBuffer::document() const noexcept { return document_; }
const std::string& EditorBuffer::identity() const noexcept { return identity_; }
const std::optional<std::string>& EditorBuffer::recovery_snapshot() const noexcept {
  return recovery_snapshot_;
}
const std::string& EditorBuffer::initialization_message() const noexcept {
  return initialization_message_;
}

std::size_t EditorBuffer::cursor() const noexcept { return cursor_; }
void EditorBuffer::set_cursor(const std::size_t cursor) noexcept {
  cursor_ = std::min(cursor, document_.buffer().text().size());
}
std::size_t EditorBuffer::desired_column() const noexcept { return desired_column_; }
void EditorBuffer::set_desired_column(const std::size_t column) noexcept { desired_column_ = column; }
std::size_t EditorBuffer::top_line() const noexcept { return top_line_; }
void EditorBuffer::set_top_line(const std::size_t line) noexcept { top_line_ = line; }
std::size_t EditorBuffer::left_column() const noexcept { return left_column_; }
void EditorBuffer::set_left_column(const std::size_t column) noexcept { left_column_ = column; }

bool EditorBuffer::checkpoint(std::string& error) {
  if (!journal_.append_snapshot(document_.buffer().text(), error)) return false;
  recovery_snapshot_ = document_.buffer().text();
  return true;
}

bool EditorBuffer::clear_checkpoint(std::string& error) { return journal_.clear(error); }

bool EditorBuffer::restore_recovery() {
  if (!recovery_snapshot_) return false;
  document_.restore_text(*recovery_snapshot_);
  set_cursor(cursor_);
  return true;
}

bool EditorBuffer::save_session(std::string& error) const {
  return save_document_session(state_directory_, identity_,
                               {.cursor_byte = cursor_, .top_line = top_line_ + 1U}, error);
}

}  // namespace horcrux
