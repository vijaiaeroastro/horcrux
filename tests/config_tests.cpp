#include "vijai/config.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void config_tests() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("vijai-config-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(root);
  const auto path = root / "vijai.json";
  {
    std::ofstream output(path);
    output << R"({
      "schemaVersion": 1,
      "editor": {"tabSize": 2, "formatOnSave": true},
      "tasks": {"build": {"command": ["cmake", "--build", "build"]}},
      "agents": {"defaultProvider": "codex", "excludes": ["secrets/**"]},
      "futureKey": true
    })";
  }
  std::string error;
  const auto config = vijai::load_project_config(path, error);
  assert(config);
  assert(config->tab_size == 2);
  assert(config->format_on_save);
  assert(config->tasks.size() == 1U);
  assert(config->tasks.front().command.size() == 3U);
  assert(config->agent_excludes.front() == "secrets/**");
  assert(config->warnings.size() == 1U);

  {
    std::ofstream output(path, std::ios::trunc);
    output << R"({"schemaVersion": 2})";
  }
  assert(!vijai::load_project_config(path, error));
  assert(error == "unsupported vijai.json schemaVersion 2");
  std::filesystem::remove_all(root);
}

struct RunConfigTests {
  RunConfigTests() { config_tests(); }
};

RunConfigTests run_config_tests;

}  // namespace
