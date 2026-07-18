#include "horcrux/project_search_job.hpp"

#include <exception>
#include <utility>

namespace horcrux {

ProjectSearchJob::~ProjectSearchJob() { wait(); }

bool ProjectSearchJob::start(std::filesystem::path root, std::string query,
                             const std::size_t maximum_matches, Completion completion) {
  if (running_.exchange(true)) return false;
  if (worker_.joinable()) worker_.join();
  worker_ = std::thread([this, root = std::move(root), query = std::move(query),
                         maximum_matches, completion = std::move(completion)]() mutable {
    ProjectSearchResult result;
    try {
      result = ProjectSearch(root).find_literal(query, maximum_matches);
    } catch (const std::exception& error) {
      result.error = "project search failed: " + std::string(error.what());
    } catch (...) {
      result.error = "project search failed unexpectedly";
    }
    running_.store(false);
    completion(std::move(result));
  });
  return true;
}

bool ProjectSearchJob::running() const noexcept { return running_.load(); }

void ProjectSearchJob::wait() {
  if (worker_.joinable()) worker_.join();
}

}  // namespace horcrux
