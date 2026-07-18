#pragma once

#include "horcrux/document.hpp"
#include "horcrux/recovery.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace horcrux {

// Owns the persisted and viewport state for one open editor document.  UI
// classes may render it, but recovery/session policy belongs here.
class EditorBuffer {
 public:
  EditorBuffer(Document document, const std::filesystem::path& state_directory,
               bool restore_session);

  [[nodiscard]] Document& document() noexcept;
  [[nodiscard]] const Document& document() const noexcept;
  [[nodiscard]] const std::string& identity() const noexcept;
  [[nodiscard]] const std::optional<std::string>& recovery_snapshot() const noexcept;
  [[nodiscard]] const std::string& initialization_message() const noexcept;

  [[nodiscard]] std::size_t cursor() const noexcept;
  void set_cursor(std::size_t cursor) noexcept;
  [[nodiscard]] std::size_t desired_column() const noexcept;
  void set_desired_column(std::size_t column) noexcept;
  [[nodiscard]] std::size_t top_line() const noexcept;
  void set_top_line(std::size_t line) noexcept;
  [[nodiscard]] std::size_t left_column() const noexcept;
  void set_left_column(std::size_t column) noexcept;

  bool checkpoint(std::string& error);
  bool clear_checkpoint(std::string& error);
  bool restore_recovery();
  bool save_session(std::string& error) const;

 private:
  Document document_;
  std::string identity_;
  RecoveryJournal journal_;
  std::optional<std::string> recovery_snapshot_;
  std::string initialization_message_;
  std::size_t cursor_{0};
  std::size_t desired_column_{0};
  std::size_t top_line_{0};
  std::size_t left_column_{0};
  std::filesystem::path state_directory_;
};

}  // namespace horcrux
