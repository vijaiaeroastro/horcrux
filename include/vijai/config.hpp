#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vijai {

struct TaskConfig {
  std::string name;
  std::vector<std::string> command;
  std::optional<std::filesystem::path> cwd;
  bool shell{false};
};

struct ProjectConfig {
  int schema_version{1};
  int tab_size{4};
  bool format_on_save{false};
  std::string default_agent_provider{"codex"};
  std::vector<std::string> agent_excludes;
  std::vector<TaskConfig> tasks;
  std::vector<std::string> warnings;
};

[[nodiscard]] std::optional<ProjectConfig> load_project_config(
    const std::filesystem::path& path, std::string& error);

}  // namespace vijai
