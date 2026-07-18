#include "horcrux/task_runner.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

namespace {

void task_runner_tests() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("horcrux-task-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(root);
  const horcrux::TaskConfig task{
      .name = "echo",
      .command = {"cmake", "-E", "echo", "hello from task"},
      .cwd = std::nullopt,
      .shell = false,
  };
  const auto result = horcrux::run_task(task, root);
  assert(result.launched);
  assert(result.exit_code == 0);
  assert(result.standard_output.find("hello from task") != std::string::npos);
  assert(result.standard_error.empty());

  const horcrux::TaskConfig escaped{
      .name = "escape",
      .command = {"cmake", "-E", "echo", "no"},
      .cwd = "..",
      .shell = false,
  };
  const auto rejected = horcrux::run_task(escaped, root);
  assert(!rejected.launched);
  assert(rejected.error == "task working directory escapes the project root");
  std::filesystem::remove_all(root);
}

struct RunTaskRunnerTests {
  RunTaskRunnerTests() { task_runner_tests(); }
};

RunTaskRunnerTests run_task_runner_tests;

}  // namespace
