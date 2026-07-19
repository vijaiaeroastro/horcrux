#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace vijai {

struct DocumentSession {
  std::size_t cursor_byte{0};
  std::size_t top_line{1};
};

[[nodiscard]] std::optional<DocumentSession> load_document_session(
    const std::filesystem::path& state_directory, const std::string& document_identity,
    std::string& error);
bool save_document_session(const std::filesystem::path& state_directory,
                           const std::string& document_identity,
                           const DocumentSession& session, std::string& error);

}  // namespace vijai
