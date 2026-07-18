#include "horcrux/project_search.hpp"

#include "horcrux/task_runner.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>

#include <nlohmann/json.hpp>

namespace horcrux {
namespace {

constexpr std::uintmax_t maximum_file_bytes = 20U * 1024U * 1024U;

bool contains_nul(const std::string& value) {
  return value.find('\0') != std::string::npos;
}

std::string preview_for(std::string value) {
  constexpr std::size_t maximum_preview = 180U;
  if (value.size() > maximum_preview) value.resize(maximum_preview);
  return value;
}

void trim_line_ending(std::string& value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
}

}  // namespace

ProjectSearch::ProjectSearch(std::filesystem::path root) : root_(std::move(root)) {}

ProjectSearchResult ProjectSearch::find_literal(const std::string& query,
                                                const std::size_t maximum_matches) const {
  ProjectSearchResult result;
  if (query.empty()) {
    result.error = "project search query is empty";
    return result;
  }
  std::error_code filesystem_error;
  if (!std::filesystem::is_directory(root_, filesystem_error)) {
    result.error = "project search root is not a directory";
    return result;
  }

  const TaskConfig ripgrep_task{
      .name = "project search",
      .command = {"rg", "--json", "--fixed-strings", "--line-number", "--column",
                  "--color=never", "--max-count", std::to_string(maximum_matches), "--",
                  query, "."},
      .cwd = std::nullopt,
      .shell = false,
  };
  const auto process = run_task(ripgrep_task, root_);
  if (process.launched) {
    if (process.exit_code != 0 && process.exit_code != 1) {
      result.error = process.standard_error.empty() ? "ripgrep search failed" : process.standard_error;
      return result;
    }
    result.used_git_ignore = true;
    std::istringstream output(process.standard_output);
    std::string record_line;
    while (std::getline(output, record_line) && result.matches.size() < maximum_matches) {
      const auto record = nlohmann::json::parse(record_line, nullptr, false);
      if (record.is_discarded() || record.value("type", "") != "match") continue;
      const auto& data = record["data"];
      if (!data.contains("path") || !data.contains("lines") || !data.contains("submatches")) continue;
      const auto path = std::filesystem::path(data["path"].value("text", "")).lexically_normal();
      std::string preview = data["lines"].value("text", "");
      trim_line_ending(preview);
      const auto line = data.value("line_number", 0U);
      for (const auto& submatch : data["submatches"]) {
        if (result.matches.size() >= maximum_matches) break;
        result.matches.push_back({
            .path = path,
            .line = line,
            .column = submatch.value("start", 0U) + 1U,
            .preview = preview_for(preview),
        });
      }
    }
    return result;
  }

  auto files = fallback_files(result.error);
  if (!result.error.empty()) return result;

  for (const auto& relative : files) {
    if (result.matches.size() >= maximum_matches) break;
    const auto path = root_ / relative;
    const auto size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error || size > maximum_file_bytes) {
      filesystem_error.clear();
      continue;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) continue;
    const std::string contents(std::istreambuf_iterator<char>(input), {});
    if (contains_nul(contents)) continue;

    std::size_t offset = 0U;
    while (offset < contents.size() && result.matches.size() < maximum_matches) {
      const auto found = contents.find(query, offset);
      if (found == std::string::npos) break;
      const auto line_start = contents.rfind('\n', found);
      const auto start = line_start == std::string::npos ? 0U : line_start + 1U;
      const auto line_end = contents.find('\n', found);
      result.matches.push_back({
          .path = relative,
          .line = static_cast<std::size_t>(
              std::count(contents.begin(), contents.begin() + static_cast<std::ptrdiff_t>(found), '\n')) +
                  1U,
          .column = found - start + 1U,
          .preview = preview_for(contents.substr(start, line_end == std::string::npos
                                                             ? std::string::npos
                                                             : line_end - start)),
      });
      offset = found + std::max<std::size_t>(1U, query.size());
    }
  }
  return result;
}

std::vector<std::filesystem::path> ProjectSearch::fallback_files(std::string& error) const {
  error.clear();
  std::vector<std::filesystem::path> files;
  std::error_code filesystem_error;
  for (std::filesystem::recursive_directory_iterator iterator(root_, filesystem_error), end;
       !filesystem_error && iterator != end; iterator.increment(filesystem_error)) {
    const auto relative = iterator->path().lexically_relative(root_);
    if (iterator->is_directory(filesystem_error) && is_ignored_fallback(relative)) {
      iterator.disable_recursion_pending();
      continue;
    }
    if (filesystem_error) {
      filesystem_error.clear();
      continue;
    }
    if (iterator->is_regular_file(filesystem_error) && !is_ignored_fallback(relative)) {
      files.push_back(relative);
    }
    filesystem_error.clear();
  }
  if (filesystem_error) error = "could not enumerate project files";
  return files;
}

bool ProjectSearch::is_ignored_fallback(const std::filesystem::path& relative) const {
  const auto name = relative.filename().string();
  return name == ".git" || name == ".deps" || name == "vcpkg_installed" ||
         name == "build" || name.starts_with("build-") || name.starts_with("cmake-build-");
}

}  // namespace horcrux
