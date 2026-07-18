#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace horcrux {

struct GitStatusEntry {
  char index_status{' '};
  char worktree_status{' '};
  std::filesystem::path path;
  std::optional<std::filesystem::path> original_path;
};

struct GitStatusResult {
  std::vector<GitStatusEntry> entries;
  std::string error;
};

struct GitCommandResult {
  bool succeeded{false};
  std::string output;
  std::string error;
};

struct GitRepositoryInfo {
  bool available{false};
  std::string branch;
  std::size_t staged{0};
  std::size_t modified{0};
  std::size_t untracked{0};
  std::string error;
};

[[nodiscard]] GitStatusResult parse_git_porcelain_z(std::string_view output);
[[nodiscard]] GitStatusResult read_git_status(const std::filesystem::path& project_root);
[[nodiscard]] GitRepositoryInfo read_git_repository_info(
    const std::filesystem::path& project_root);
[[nodiscard]] std::string format_git_status(const GitStatusResult& status);
[[nodiscard]] GitCommandResult stage_git_file(const std::filesystem::path& project_root,
                                              const std::filesystem::path& file);
[[nodiscard]] GitCommandResult unstage_git_file(const std::filesystem::path& project_root,
                                                const std::filesystem::path& file);
[[nodiscard]] GitCommandResult commit_git(const std::filesystem::path& project_root,
                                          const std::string& message);

}  // namespace horcrux
