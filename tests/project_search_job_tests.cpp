#include "horcrux/project_search_job.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>

namespace {

void project_search_job_tests() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("horcrux-search-job-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  std::ofstream(root / "source.txt") << "find this value\n";
  std::promise<horcrux::ProjectSearchResult> completion;
  auto completed = completion.get_future();
  horcrux::ProjectSearchJob job;
  assert(job.start(root, "value", 10U,
                   [&completion](horcrux::ProjectSearchResult result) {
                     completion.set_value(std::move(result));
                   }));
  const auto result = completed.get();
  job.wait();
  assert(result.error.empty());
  assert(result.matches.size() == 1U);
  assert(!job.running());
  std::filesystem::remove_all(root);
}

struct RunProjectSearchJobTests {
  RunProjectSearchJobTests() { project_search_job_tests(); }
};

RunProjectSearchJobTests run_project_search_job_tests;

}  // namespace
