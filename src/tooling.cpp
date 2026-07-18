#include "horcrux/tooling.hpp"

#include <cstdlib>
#include <string_view>

namespace horcrux {
namespace {

char path_separator() {
#ifdef _WIN32
  return ';';
#else
  return ':';
#endif
}

bool is_executable_file(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error) || error) return false;
#ifdef _WIN32
  return true;
#else
  const auto permissions = std::filesystem::status(path, error).permissions();
  if (error) return false;
  using std::filesystem::perms;
  return (permissions & (perms::owner_exec | perms::group_exec | perms::others_exec)) != perms::none;
#endif
}

std::vector<std::string> candidate_names(const std::string& name) {
#ifdef _WIN32
  if (std::filesystem::path(name).has_extension()) return {name};
  return {name + ".exe", name + ".cmd", name + ".bat"};
#else
  return {name};
#endif
}

}  // namespace

std::filesystem::path find_executable(const std::string& name) {
  const std::filesystem::path requested(name);
  if (requested.has_parent_path()) {
    return is_executable_file(requested) ? requested : std::filesystem::path{};
  }
  const char* environment = std::getenv("PATH");
  if (environment == nullptr) return {};
  const std::string path_value(environment);
  std::size_t start = 0;
  while (start <= path_value.size()) {
    const auto end = path_value.find(path_separator(), start);
    const auto directory = path_value.substr(start, end == std::string::npos ? std::string::npos : end - start);
    for (const auto& candidate : candidate_names(name)) {
      const auto path = (directory.empty() ? std::filesystem::current_path() : std::filesystem::path(directory)) /
                        candidate;
      if (is_executable_file(path)) return path;
    }
    if (end == std::string::npos) break;
    start = end + 1U;
  }
  return {};
}

std::vector<ToolStatus> detect_developer_tools() {
  constexpr std::string_view names[] = {
      "git", "cmake", "ninja", "clangd", "clang-format", "gdb", "python3", "debugpy",
      "gopls", "node", "typescript-language-server", "prettier", "codex", "copilot"};
  std::vector<ToolStatus> tools;
  tools.reserve(std::size(names));
  for (const auto name : names) {
    tools.push_back({.name = std::string(name), .path = find_executable(std::string(name))});
  }
  return tools;
}

}  // namespace horcrux
