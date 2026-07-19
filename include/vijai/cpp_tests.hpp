#pragma once

#include <cstddef>
#include <atomic>
#include <filesystem>
#include <optional>
#include <mutex>
#include <string>
#include <thread>
#include <string_view>
#include <vector>

namespace vijai {

enum class CppTestState { not_run, running, passed, failed };

struct CppTest {
  std::string name;
  std::filesystem::path definition_file;
  std::size_t definition_line{0};
  CppTestState state{CppTestState::not_run};
};

struct CppTestDiscovery {
  std::filesystem::path build_directory;
  std::vector<CppTest> tests;
  std::string error;
};

struct CppTestRun {
  bool succeeded{false};
  std::vector<CppTest> tests;
  std::string output;
  std::string error;
};

struct CppTestProgress {
  std::size_t index{0};
  CppTestState state{CppTestState::not_run};
  std::string output;
  std::string error;
};

// Executes tests one at a time off the UI thread so the frontend can show
// progress and individual results as they complete.
class CppTestJob {
 public:
  CppTestJob() = default;
  ~CppTestJob();
  CppTestJob(const CppTestJob&) = delete;
  CppTestJob& operator=(const CppTestJob&) = delete;

  [[nodiscard]] bool start(std::filesystem::path project_root,
                           std::filesystem::path build_directory,
                           std::vector<CppTest> tests,
                           std::optional<std::size_t> selected_index,
                           std::size_t timeout_seconds = 30U);
  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] std::vector<CppTestProgress> take_progress();
  void wait();

 private:
  std::atomic<bool> running_{false};
  std::mutex progress_mutex_;
  std::vector<CppTestProgress> progress_;
  std::thread worker_;
};

[[nodiscard]] std::vector<CppTest> parse_ctest_test_listing(std::string_view json);
[[nodiscard]] std::vector<std::filesystem::path> find_cpp_test_build_directories(
    const std::filesystem::path& project_root);
[[nodiscard]] CppTestDiscovery discover_cpp_tests_in_directory(
    const std::filesystem::path& project_root, const std::filesystem::path& build_directory);
[[nodiscard]] CppTestDiscovery discover_cpp_tests(const std::filesystem::path& project_root);
[[nodiscard]] CppTestRun run_cpp_tests(const std::filesystem::path& project_root,
                                        const std::filesystem::path& build_directory,
                                        const std::vector<CppTest>& known_tests,
                                        std::optional<std::string> selected_test = std::nullopt,
                                        std::size_t timeout_seconds = 30U);

}  // namespace vijai
