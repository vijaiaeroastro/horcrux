#include "horcrux/project_search_job.hpp"

#include <utility>

namespace horcrux {

ProjectSearchJob::~ProjectSearchJob() { wait(); }

bool ProjectSearchJob::start(std::filesystem::path root, std::string query,
                             const std::size_t maximum_matches, Completion completion) {
  if (running_.exchange(true)) return false;
  if (worker_.joinable()) worker_.join();
  worker_ = std::jthread([this, root = std::move(root), query = std::move(query),
                          maximum_matches, completion = std::move(completion)](
                             const std::stop_token stop_token) mutable {
    ProjectSearchResult result;
    if (!stop_token.stop_requested()) {
      result = ProjectSearch(root).find_literal(query, maximum_matches);
    } else {
      result.error = "project search cancelled";
    }
    running_.store(false);
    if (!stop_token.stop_requested()) completion(std::move(result));
  });
  return true;
}

bool ProjectSearchJob::running() const noexcept { return running_.load(); }

void ProjectSearchJob::wait() {
  if (worker_.joinable()) worker_.join();
}

}  // namespace horcrux
