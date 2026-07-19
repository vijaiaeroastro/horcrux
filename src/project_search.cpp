#include "vijai/project_search.hpp"

#include "vijai/task_runner.hpp"

#include <sstream>
#include <string_view>

#include <nlohmann/json.hpp>

namespace vijai {
namespace {

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
  if (!process.launched) {
    result.error = "Ripgrep (rg) is not available on PATH. Install ripgrep to search this project.";
    return result;
  }
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

}  // namespace vijai
