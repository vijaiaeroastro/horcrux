#include "vijai/cpp_tests.hpp"

#include "vijai/config.hpp"
#include "vijai/task_runner.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

#include <nlohmann/json.hpp>

namespace vijai {
namespace {

bool likely_cmake_build_directory(const std::filesystem::path& path) {
  const auto name = path.filename().string();
  return name == "build" || name.starts_with("build-") || name.starts_with("cmake-build-");
}

std::vector<std::filesystem::path> candidate_build_directories(
    const std::filesystem::path& project_root) {
  std::vector<std::filesystem::path> candidates;
  for (const auto& preferred : {"build-vcpkg", "build", "cmake-build-debug", "cmake-build-release"}) {
    const auto path = project_root / preferred;
    if (std::filesystem::exists(path / "CTestTestfile.cmake")) candidates.push_back(path);
  }
  std::error_code error;
  for (std::filesystem::directory_iterator it(project_root, error), end; !error && it != end;
       it.increment(error)) {
    if (!it->is_directory(error) || !likely_cmake_build_directory(it->path()) ||
        !std::filesystem::exists(it->path() / "CTestTestfile.cmake")) {
      continue;
    }
    if (std::find(candidates.begin(), candidates.end(), it->path()) == candidates.end()) {
      candidates.push_back(it->path());
    }
  }
  return candidates;
}

std::string escape_ctest_regex(const std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() * 2U);
  for (const char character : value) {
    if (std::string_view("\\.^$|()[]{}*+?").find(character) != std::string_view::npos) {
      escaped.push_back('\\');
    }
    escaped.push_back(character);
  }
  return escaped;
}

}  // namespace

std::vector<CppTest> parse_ctest_test_listing(const std::string_view json) {
  const auto document = nlohmann::json::parse(json, nullptr, false);
  if (document.is_discarded() || !document.contains("tests") || !document["tests"].is_array()) {
    return {};
  }
  std::vector<CppTest> tests;
  for (const auto& test : document["tests"]) {
    const auto name = test.value("name", "");
    if (name.empty()) continue;
    CppTest parsed{.name = name,
                   .definition_file = {},
                   .definition_line = 0U,
                   .state = CppTestState::not_run};
    if (test.contains("backtrace") && test["backtrace"].is_number_unsigned() &&
        document.contains("backtraceGraph") && document["backtraceGraph"].is_object()) {
      const auto& graph = document["backtraceGraph"];
      const auto node_index = test["backtrace"].get<std::size_t>();
      if (graph.contains("nodes") && graph["nodes"].is_array() && node_index < graph["nodes"].size()) {
        const auto& node = graph["nodes"][node_index];
        if (node.contains("file") && node["file"].is_number_unsigned() &&
            graph.contains("files") && graph["files"].is_array()) {
          const auto file_index = node["file"].get<std::size_t>();
          if (file_index < graph["files"].size() && graph["files"][file_index].is_string()) {
            parsed.definition_file = graph["files"][file_index].get<std::string>();
          }
        }
        if (node.contains("line") && node["line"].is_number_unsigned()) {
          parsed.definition_line = node["line"].get<std::size_t>();
        }
      }
    }
    tests.push_back(std::move(parsed));
  }
  return tests;
}

std::vector<std::filesystem::path> find_cpp_test_build_directories(
    const std::filesystem::path& project_root) {
  return candidate_build_directories(project_root);
}

CppTestDiscovery discover_cpp_tests_in_directory(const std::filesystem::path& project_root,
                                                 const std::filesystem::path& directory) {
  CppTestDiscovery discovery;
  const TaskConfig task{
      .name = "discover C++ tests",
      .command = {"ctest", "--test-dir", directory.string(), "--show-only=json-v1"},
      .cwd = std::nullopt,
      .shell = false,
  };
  const auto result = run_task(task, project_root);
  if (!result.launched || result.exit_code != 0) {
    discovery.error = result.error.empty() ? "Could not discover CTest tests in " +
                                                directory.filename().string()
                                           : result.error;
    return discovery;
  }
  discovery.build_directory = directory;
  discovery.tests = parse_ctest_test_listing(result.standard_output);
  if (discovery.tests.empty()) discovery.error = "CTest found no registered C++ tests.";
  return discovery;
}

CppTestDiscovery discover_cpp_tests(const std::filesystem::path& project_root) {
  const auto candidates = find_cpp_test_build_directories(project_root);
  if (candidates.empty()) {
    CppTestDiscovery discovery;
    discovery.error = "No CTest build directory found. Configure the CMake project first.";
    return discovery;
  }
  CppTestDiscovery last;
  for (const auto& directory : candidates) {
    auto discovery = discover_cpp_tests_in_directory(project_root, directory);
    if (!discovery.tests.empty()) return discovery;
    last = std::move(discovery);
  }
  return last;
}

CppTestRun run_cpp_tests(const std::filesystem::path& project_root,
                         const std::filesystem::path& build_directory,
                         const std::vector<CppTest>& known_tests,
                         std::optional<std::string> selected_test,
                         const std::size_t timeout_seconds) {
  CppTestRun run;
  run.tests = known_tests;
  std::vector<std::string> command{"ctest", "--test-dir", build_directory.string(),
                                   "--output-on-failure", "--timeout",
                                   std::to_string(timeout_seconds)};
  if (selected_test) {
    command.insert(command.end(), {"-R", "^" + escape_ctest_regex(*selected_test) + "$"});
  }
  const TaskConfig task{.name = "run C++ tests", .command = std::move(command),
                        .cwd = std::nullopt, .shell = false};
  const auto result = run_task(task, project_root);
  run.succeeded = result.launched && result.exit_code == 0;
  run.output = result.standard_output;
  if (!result.standard_error.empty()) {
    if (!run.output.empty()) run.output += '\n';
    run.output += result.standard_error;
  }
  run.error = result.error;
  for (auto& test : run.tests) {
    if (!selected_test || test.name == *selected_test) {
      test.state = run.succeeded ? CppTestState::passed : CppTestState::failed;
    }
  }
  return run;
}

CppTestJob::~CppTestJob() { wait(); }

bool CppTestJob::start(std::filesystem::path project_root, std::filesystem::path build_directory,
                       std::vector<CppTest> tests,
                       const std::optional<std::size_t> selected_index,
                       const std::size_t timeout_seconds) {
  if (running_.exchange(true)) return false;
  if (worker_.joinable()) worker_.join();
  {
    std::scoped_lock lock(progress_mutex_);
    progress_.clear();
  }
  worker_ = std::thread([this, project_root = std::move(project_root),
                         build_directory = std::move(build_directory), tests = std::move(tests),
                         selected_index, timeout_seconds]() mutable {
    std::vector<std::size_t> indices;
    if (selected_index && *selected_index < tests.size()) {
      indices.push_back(*selected_index);
    } else if (!selected_index) {
      for (std::size_t index = 0; index < tests.size(); ++index) indices.push_back(index);
    }
    for (const auto index : indices) {
      {
        std::scoped_lock lock(progress_mutex_);
        progress_.push_back({.index = index,
                             .state = CppTestState::running,
                             .output = {},
                             .error = {}});
      }
      const auto result = run_cpp_tests(project_root, build_directory, tests, tests[index].name,
                                        timeout_seconds);
      const auto state = result.succeeded ? CppTestState::passed : CppTestState::failed;
      {
        std::scoped_lock lock(progress_mutex_);
        progress_.push_back({.index = index,
                             .state = state,
                             .output = result.output,
                             .error = result.error});
      }
    }
    running_.store(false);
  });
  return true;
}

bool CppTestJob::running() const noexcept { return running_.load(); }

std::vector<CppTestProgress> CppTestJob::take_progress() {
  std::scoped_lock lock(progress_mutex_);
  auto progress = std::move(progress_);
  progress_.clear();
  return progress;
}

void CppTestJob::wait() {
  if (worker_.joinable()) worker_.join();
}

}  // namespace vijai
