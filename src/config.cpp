#include "horcrux/config.hpp"

#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

namespace horcrux {
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
  warn_unknown_keys(root, {"$schema", "schemaVersion", "editor", "tasks", "agents"}, "",
                    config.warnings);
  if (!root.contains("schemaVersion") || !root["schemaVersion"].is_number_integer()) {
    error = "schemaVersion must be the integer 1";
    return std::nullopt;
  }
  config.schema_version = root["schemaVersion"].get<int>();
  if (config.schema_version != 1) {
    error = "unsupported horcrux.json schemaVersion " + std::to_string(config.schema_version);
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
  return config;
}

}  // namespace horcrux
