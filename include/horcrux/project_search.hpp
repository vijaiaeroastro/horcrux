#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace horcrux {

struct ProjectSearchMatch {
  std::filesystem::path path;
  std::size_t line{0};
  std::size_t column{0};
  std::string preview;
};

struct ProjectSearchResult {
  std::vector<ProjectSearchMatch> matches;
  std::string error;
  bool used_git_ignore{false};
};

// Uses ripgrep for fast, Git-ignore-aware project search, invoked only with
// explicit argv. Vijai reports a clear error when ripgrep is unavailable.
class ProjectSearch {
 public:
  explicit ProjectSearch(std::filesystem::path root);

  [[nodiscard]] ProjectSearchResult find_literal(const std::string& query,
                                                 std::size_t maximum_matches = 500U) const;

 private:
  std::filesystem::path root_;
};

}  // namespace horcrux
