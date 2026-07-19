#include "vijai/session.hpp"

#include "vijai/recovery.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

namespace vijai {
namespace {

std::filesystem::path session_path(const std::filesystem::path& state_directory,
                                   const std::string& identity) {
  return state_directory / "sessions" / (document_state_key(identity) + ".json");
}

}  // namespace

std::optional<DocumentSession> load_document_session(
    const std::filesystem::path& state_directory, const std::string& document_identity,
    std::string& error) {
  error.clear();
  std::ifstream input(session_path(state_directory, document_identity));
  if (!input) return std::nullopt;
  try {
    const auto json = nlohmann::json::parse(input);
    if (!json.is_object() || json.value("schemaVersion", 0) != 1 ||
        !json.contains("cursorByte") || !json["cursorByte"].is_number_unsigned() ||
        !json.contains("topLine") || !json["topLine"].is_number_unsigned()) {
      error = "document session has an invalid schema";
      return std::nullopt;
    }
    return DocumentSession{
        .cursor_byte = json["cursorByte"].get<std::size_t>(),
        .top_line = std::max<std::size_t>(1U, json["topLine"].get<std::size_t>()),
    };
  } catch (const nlohmann::json::exception& exception) {
    error = "could not parse document session: " + std::string(exception.what());
    return std::nullopt;
  }
}

bool save_document_session(const std::filesystem::path& state_directory,
                           const std::string& document_identity,
                           const DocumentSession& session, std::string& error) {
  error.clear();
  const auto target = session_path(state_directory, document_identity);
  std::error_code filesystem_error;
  std::filesystem::create_directories(target.parent_path(), filesystem_error);
  if (filesystem_error) {
    error = "could not create session directory";
    return false;
  }
  const auto temporary = target.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
      error = "could not create document session";
      return false;
    }
    const nlohmann::json json = {
        {"schemaVersion", 1},
        {"cursorByte", session.cursor_byte},
        {"topLine", session.top_line},
    };
    output << json.dump(2) << '\n';
    if (!output) {
      error = "could not write document session";
      return false;
    }
  }
#ifdef _WIN32
  std::filesystem::remove(target, filesystem_error);
  filesystem_error.clear();
#endif
  std::filesystem::rename(temporary, target, filesystem_error);
  if (filesystem_error) {
    std::filesystem::remove(temporary, filesystem_error);
    error = "could not replace document session";
    return false;
  }
  return true;
}

}  // namespace vijai
