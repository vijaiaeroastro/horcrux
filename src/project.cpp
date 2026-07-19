#include "vijai/project.hpp"

#include <fstream>
#include <set>

namespace vijai {
namespace {

std::filesystem::path canonical_or_absolute(const std::filesystem::path& path) {
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(path, error);
  if (!error) return canonical;
  return std::filesystem::absolute(path, error);
}

std::filesystem::path trust_file(const std::filesystem::path& state_directory) {
  return state_directory / "trusted-projects.txt";
}

std::set<std::string> read_trusted_projects(const std::filesystem::path& state_directory) {
  std::set<std::string> projects;
  std::ifstream input(trust_file(state_directory));
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty()) projects.insert(line);
  }
  return projects;
}

}  // namespace

std::optional<std::filesystem::path> discover_project_root(const std::filesystem::path& start) {
  std::error_code error;
  const bool start_is_directory = std::filesystem::is_directory(start, error);
  error.clear();  // A not-yet-created file is a valid editor target.
  auto candidate = start_is_directory ? start : start.parent_path();
  if (candidate.empty()) candidate = std::filesystem::current_path(error);
  if (error) return std::nullopt;
  auto current = canonical_or_absolute(candidate);
  if (current.empty()) return std::nullopt;
  while (true) {
    if (std::filesystem::exists(current / ".git", error) ||
        std::filesystem::exists(current / "vijai.json", error)) {
      return current;
    }
    const auto parent = current.parent_path();
    if (parent == current) break;
    current = parent;
  }
  return std::nullopt;
}

bool is_project_trusted(const std::filesystem::path& state_directory,
                        const std::filesystem::path& project_root) {
  return read_trusted_projects(state_directory).contains(canonical_or_absolute(project_root).string());
}

bool set_project_trusted(const std::filesystem::path& state_directory,
                         const std::filesystem::path& project_root, const bool trusted,
                         std::string& error) {
  std::error_code filesystem_error;
  std::filesystem::create_directories(state_directory, filesystem_error);
  if (filesystem_error) {
    error = "could not create Vijai state directory " + state_directory.string();
    return false;
  }

  auto projects = read_trusted_projects(state_directory);
  const auto normalized = canonical_or_absolute(project_root).string();
  if (trusted) {
    projects.insert(normalized);
  } else {
    projects.erase(normalized);
  }

  const auto target = trust_file(state_directory);
  const auto temporary = target.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
      error = "could not write Vijai trust state";
      return false;
    }
    for (const auto& project : projects) output << project << '\n';
    if (!output) {
    error = "could not finish Vijai trust state";
      return false;
    }
  }

#ifdef _WIN32
  std::filesystem::remove(target, filesystem_error);
  filesystem_error.clear();
#endif
  std::filesystem::rename(temporary, target, filesystem_error);
  if (filesystem_error) {
    std::filesystem::remove(temporary, filesystem_error);
    error = "could not save Vijai trust state";
    return false;
  }
  return true;
}

}  // namespace vijai
