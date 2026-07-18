#pragma once

#include "horcrux/project_search.hpp"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

namespace horcrux {

// Runs one project search off the UI thread. The completion callback executes
// on the worker, allowing the frontend to marshal the result to its own loop.
class ProjectSearchJob {
 public:
  using Completion = std::function<void(ProjectSearchResult)>;

  ProjectSearchJob() = default;
  ~ProjectSearchJob();
  ProjectSearchJob(const ProjectSearchJob&) = delete;
  ProjectSearchJob& operator=(const ProjectSearchJob&) = delete;

  [[nodiscard]] bool start(std::filesystem::path root, std::string query,
                           std::size_t maximum_matches, Completion completion);
  [[nodiscard]] bool running() const noexcept;
  void wait();

 private:
  std::atomic<bool> running_{false};
  std::jthread worker_;
};

}  // namespace horcrux
