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
  std::optional<std::filesystem::path> cpp_test_build_directory;
  std::string theme{"midnight"};
  int tool_height{12};
  bool tool_height_locked{true};
  bool explorer_visible{true};
  bool show_hidden_files{false};
  bool tool_window_visible{false};
  std::string active_tool_window{"search"};
  std::vector<std::filesystem::path> open_files;
  std::optional<std::filesystem::path> active_file;
  std::optional<std::filesystem::path> last_opened_file;
  std::vector<std::string> warnings;
};

[[nodiscard]] std::optional<ProjectConfig> load_project_config(
    const std::filesystem::path& path, std::string& error);

// Saves editor-owned workspace state while preserving user-owned configuration
// such as tasks and agent settings already present in vijai.json.
bool save_project_workspace_state(const std::filesystem::path& path,
                                  const ProjectConfig& config, std::string& error);

}  // namespace vijai
