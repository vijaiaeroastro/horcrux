#include "vijai/config.hpp"

#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

namespace vijai {
namespace {

using Json = nlohmann::json;

bool require_object(const Json& value, const std::string& name, std::string& error) {
  if (value.is_object()) return true;
  error = name + " must be a JSON object";
  return false;
}

void warn_unknown_keys(const Json& value, const std::set<std::string>& known,
                       const std::string& prefix, std::vector<std::string>& warnings) {
  for (const auto& [key, ignored] : value.items()) {
    static_cast<void>(ignored);
    if (!known.contains(key)) warnings.push_back("unknown key: " + prefix + key);
  }
}

}  // namespace

std::optional<ProjectConfig> load_project_config(const std::filesystem::path& path,
                                                 std::string& error) {
  error.clear();
  std::ifstream input(path);
  if (!input) {
    error = "could not open project config " + path.string();
    return std::nullopt;
  }

  Json root;
  try {
    input >> root;
  } catch (const Json::parse_error& exception) {
    error = "invalid JSON in " + path.string() + ": " + exception.what();
    return std::nullopt;
  }
  if (!require_object(root, "project config", error)) return std::nullopt;

  ProjectConfig config;
  warn_unknown_keys(root, {"$schema", "schemaVersion", "editor", "tasks", "agents", "tests", "workspace"}, "",
                    config.warnings);
  if (!root.contains("schemaVersion") || !root["schemaVersion"].is_number_integer()) {
    error = "schemaVersion must be the integer 1";
    return std::nullopt;
  }
  config.schema_version = root["schemaVersion"].get<int>();
  if (config.schema_version != 1) {
    error = "unsupported vijai.json schemaVersion " + std::to_string(config.schema_version);
    return std::nullopt;
  }

  if (root.contains("editor")) {
    const auto& editor = root["editor"];
    if (!require_object(editor, "editor", error)) return std::nullopt;
    warn_unknown_keys(editor, {"tabSize", "formatOnSave"}, "editor.", config.warnings);
    if (editor.contains("tabSize")) {
      if (!editor["tabSize"].is_number_integer()) {
        error = "editor.tabSize must be an integer from 1 to 16";
        return std::nullopt;
      }
      config.tab_size = editor["tabSize"].get<int>();
      if (config.tab_size < 1 || config.tab_size > 16) {
        error = "editor.tabSize must be an integer from 1 to 16";
        return std::nullopt;
      }
    }
    if (editor.contains("formatOnSave")) {
      if (!editor["formatOnSave"].is_boolean()) {
        error = "editor.formatOnSave must be a boolean";
        return std::nullopt;
      }
      config.format_on_save = editor["formatOnSave"].get<bool>();
    }
  }

  if (root.contains("tasks")) {
    const auto& tasks = root["tasks"];
    if (!require_object(tasks, "tasks", error)) return std::nullopt;
    for (const auto& [name, task] : tasks.items()) {
      if (!require_object(task, "task " + name, error)) return std::nullopt;
      warn_unknown_keys(task, {"command", "cwd", "shell"}, "tasks." + name + ".", config.warnings);
      if (!task.contains("command") || !task["command"].is_array() || task["command"].empty()) {
        error = "tasks." + name + ".command must be a non-empty string array";
        return std::nullopt;
      }
      TaskConfig parsed{.name = name, .command = {}, .cwd = std::nullopt, .shell = false};
      for (const auto& argument : task["command"]) {
        if (!argument.is_string()) {
          error = "tasks." + name + ".command must contain only strings";
          return std::nullopt;
        }
        parsed.command.push_back(argument.get<std::string>());
      }
      if (task.contains("cwd")) {
        if (!task["cwd"].is_string()) {
          error = "tasks." + name + ".cwd must be a string";
          return std::nullopt;
        }
        parsed.cwd = task["cwd"].get<std::string>();
      }
      if (task.contains("shell")) {
        if (!task["shell"].is_boolean()) {
          error = "tasks." + name + ".shell must be a boolean";
          return std::nullopt;
        }
        parsed.shell = task["shell"].get<bool>();
      }
      config.tasks.push_back(std::move(parsed));
    }
  }

  if (root.contains("agents")) {
    const auto& agents = root["agents"];
    if (!require_object(agents, "agents", error)) return std::nullopt;
    warn_unknown_keys(agents, {"defaultProvider", "excludes"}, "agents.", config.warnings);
    if (agents.contains("defaultProvider")) {
      if (!agents["defaultProvider"].is_string()) {
        error = "agents.defaultProvider must be a string";
        return std::nullopt;
      }
      config.default_agent_provider = agents["defaultProvider"].get<std::string>();
      if (config.default_agent_provider != "codex" && config.default_agent_provider != "copilot") {
        error = "agents.defaultProvider must be codex or copilot";
        return std::nullopt;
      }
    }
    if (agents.contains("excludes")) {
      if (!agents["excludes"].is_array()) {
        error = "agents.excludes must be a string array";
        return std::nullopt;
      }
      for (const auto& excluded : agents["excludes"]) {
        if (!excluded.is_string()) {
          error = "agents.excludes must contain only strings";
          return std::nullopt;
        }
        config.agent_excludes.push_back(excluded.get<std::string>());
      }
    }
  }
  if (root.contains("tests")) {
    const auto& tests = root["tests"];
    if (!require_object(tests, "tests", error)) return std::nullopt;
    warn_unknown_keys(tests, {"cppBuildDirectory"}, "tests.", config.warnings);
    if (tests.contains("cppBuildDirectory")) {
      if (!tests["cppBuildDirectory"].is_string()) {
        error = "tests.cppBuildDirectory must be a string";
        return std::nullopt;
      }
      config.cpp_test_build_directory = tests["cppBuildDirectory"].get<std::string>();
    }
  }
  if (root.contains("workspace")) {
    const auto& workspace = root["workspace"];
    if (!require_object(workspace, "workspace", error)) return std::nullopt;
    warn_unknown_keys(workspace, {"theme", "toolHeight", "toolHeightLocked", "explorerVisible",
                                  "showHiddenFiles", "toolWindowVisible", "activeToolWindow",
                                  "openFiles", "activeFile", "lastOpenedFile"}, "workspace.",
                      config.warnings);
    if (workspace.contains("theme")) {
      if (!workspace["theme"].is_string()) {
        error = "workspace.theme must be a string";
        return std::nullopt;
      }
      config.theme = workspace["theme"].get<std::string>();
      if (config.theme != "midnight" && config.theme != "highContrast" &&
          config.theme != "paper") {
        error = "workspace.theme must be midnight, highContrast, or paper";
        return std::nullopt;
      }
    }
    if (workspace.contains("toolHeight")) {
      if (!workspace["toolHeight"].is_number_integer()) {
        error = "workspace.toolHeight must be an integer from 1 to 200";
        return std::nullopt;
      }
      config.tool_height = workspace["toolHeight"].get<int>();
      if (config.tool_height < 1 || config.tool_height > 200) {
        error = "workspace.toolHeight must be an integer from 1 to 200";
        return std::nullopt;
      }
    }
    if (workspace.contains("toolHeightLocked")) {
      if (!workspace["toolHeightLocked"].is_boolean()) {
        error = "workspace.toolHeightLocked must be a boolean";
        return std::nullopt;
      }
      config.tool_height_locked = workspace["toolHeightLocked"].get<bool>();
    }
    const auto read_boolean = [&workspace, &error](const char* key, bool& target) {
      if (!workspace.contains(key)) return true;
      if (!workspace[key].is_boolean()) {
        error = std::string("workspace.") + key + " must be a boolean";
        return false;
      }
      target = workspace[key].get<bool>();
      return true;
    };
    if (!read_boolean("explorerVisible", config.explorer_visible) ||
        !read_boolean("showHiddenFiles", config.show_hidden_files) ||
        !read_boolean("toolWindowVisible", config.tool_window_visible)) {
      return std::nullopt;
    }
    if (workspace.contains("activeToolWindow")) {
      if (!workspace["activeToolWindow"].is_string()) {
        error = "workspace.activeToolWindow must be a string";
        return std::nullopt;
      }
      config.active_tool_window = workspace["activeToolWindow"].get<std::string>();
      if (config.active_tool_window != "find" && config.active_tool_window != "search" &&
          config.active_tool_window != "tests" && config.active_tool_window != "git" &&
          config.active_tool_window != "tasks" && config.active_tool_window != "shell") {
        error = "workspace.activeToolWindow is not supported";
        return std::nullopt;
      }
    }
    if (workspace.contains("openFiles")) {
      if (!workspace["openFiles"].is_array()) {
        error = "workspace.openFiles must be a string array";
        return std::nullopt;
      }
      for (const auto& file : workspace["openFiles"]) {
        if (!file.is_string()) {
          error = "workspace.openFiles must contain only strings";
          return std::nullopt;
        }
        config.open_files.emplace_back(file.get<std::string>());
      }
    }
    if (workspace.contains("activeFile")) {
      if (!workspace["activeFile"].is_string()) {
        error = "workspace.activeFile must be a string";
        return std::nullopt;
      }
      config.active_file = workspace["activeFile"].get<std::string>();
    }
    if (workspace.contains("lastOpenedFile")) {
      if (!workspace["lastOpenedFile"].is_string()) {
        error = "workspace.lastOpenedFile must be a string";
        return std::nullopt;
      }
      config.last_opened_file = workspace["lastOpenedFile"].get<std::string>();
    }
  }
  return config;
}

bool save_project_workspace_state(const std::filesystem::path& path,
                                  const ProjectConfig& config, std::string& error) {
  error.clear();
  Json root = Json::object();
  if (std::filesystem::exists(path)) {
    std::ifstream input(path);
    if (!input) {
      error = "could not read project config " + path.string();
      return false;
    }
    try {
      input >> root;
    } catch (const Json::parse_error& exception) {
      error = "invalid JSON in " + path.string() + ": " + exception.what();
      return false;
    }
    if (!root.is_object()) {
      error = "project config must be a JSON object";
      return false;
    }
  }
  root["schemaVersion"] = 1;
  if (config.cpp_test_build_directory) {
    root["tests"]["cppBuildDirectory"] = config.cpp_test_build_directory->generic_string();
  }
  root["workspace"]["theme"] = config.theme;
  root["workspace"]["toolHeight"] = config.tool_height;
  root["workspace"]["toolHeightLocked"] = config.tool_height_locked;
  root["workspace"]["explorerVisible"] = config.explorer_visible;
  root["workspace"]["showHiddenFiles"] = config.show_hidden_files;
  root["workspace"]["toolWindowVisible"] = config.tool_window_visible;
  root["workspace"]["activeToolWindow"] = config.active_tool_window;
  root["workspace"]["openFiles"] = nlohmann::json::array();
  for (const auto& file : config.open_files) {
    root["workspace"]["openFiles"].push_back(file.generic_string());
  }
  if (config.active_file) {
    root["workspace"]["activeFile"] = config.active_file->generic_string();
  } else {
    root["workspace"].erase("activeFile");
  }
  if (config.last_opened_file) {
    root["workspace"]["lastOpenedFile"] = config.last_opened_file->generic_string();
  } else {
    root["workspace"].erase("lastOpenedFile");
  }
  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    error = "could not write project config " + path.string();
    return false;
  }
  output << root.dump(2) << '\n';
  if (!output) {
    error = "could not write project config " + path.string();
    return false;
  }
  return true;
}

}  // namespace vijai
