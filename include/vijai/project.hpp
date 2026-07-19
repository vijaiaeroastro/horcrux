#pragma once

#include "vijai/config.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace vijai {

struct ProjectContext {
  std::optional<std::filesystem::path> root;
  std::optional<std::filesystem::path> workspace_root;
  std::optional<ProjectConfig> config;
  std::string config_error;
  bool trusted{false};

  [[nodiscard]] bool has_project() const noexcept { return root.has_value(); }
};

[[nodiscard]] std::optional<std::filesystem::path> discover_project_root(
    const std::filesystem::path& start);
[[nodiscard]] bool is_project_trusted(const std::filesystem::path& state_directory,
                                      const std::filesystem::path& project_root);
bool set_project_trusted(const std::filesystem::path& state_directory,
                         const std::filesystem::path& project_root, bool trusted,
                         std::string& error);

}  // namespace vijai
