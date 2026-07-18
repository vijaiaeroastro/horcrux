#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace horcrux {

class RecoveryJournal {
 public:
  RecoveryJournal(std::filesystem::path state_directory, std::string document_identity);

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] std::optional<std::string> latest_snapshot(std::string& error) const;
  bool append_snapshot(std::string_view text, std::string& error);
  bool clear(std::string& error);

 private:
  std::filesystem::path path_;
  std::string identity_;
};

[[nodiscard]] std::string recovery_identity(const std::filesystem::path& document_path);
[[nodiscard]] std::string document_state_key(std::string_view document_identity);

}  // namespace horcrux
