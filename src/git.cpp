#include "vijai/git.hpp"

#include "vijai/config.hpp"
#include "vijai/task_runner.hpp"

namespace vijai {
namespace {

std::optional<std::string_view> next_record(const std::string_view output, std::size_t& offset) {
  if (offset >= output.size()) return std::nullopt;
  const auto end = output.find('\0', offset);
  if (end == std::string_view::npos) return std::nullopt;
  const auto record = output.substr(offset, end - offset);
  offset = end + 1U;
  return record;
}

bool is_rename_or_copy(const char status) { return status == 'R' || status == 'C'; }

std::string trim_line(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
  return value;
}

std::optional<std::filesystem::path> project_relative_path(
    const std::filesystem::path& project_root, const std::filesystem::path& file,
    std::string& error) {
  std::error_code filesystem_error;
  const auto root = std::filesystem::weakly_canonical(project_root, filesystem_error);
  if (filesystem_error) {
    error = "could not resolve Git project root";
    return std::nullopt;
  }
  const auto absolute_file = std::filesystem::weakly_canonical(
      file.is_absolute() ? file : std::filesystem::current_path() / file, filesystem_error);
  if (filesystem_error) {
    error = "could not resolve Git file path";
    return std::nullopt;
  }
  const auto relative = absolute_file.lexically_relative(root);
  if (relative.empty() || *relative.begin() == "..") {
    error = "refusing Git operation outside the project root";
    return std::nullopt;
  }
  return relative;
}

GitCommandResult run_git_file_command(const std::filesystem::path& project_root,
                                      const std::vector<std::string>& command) {
  const TaskConfig task{
      .name = "git file operation",
      .command = command,
      .cwd = std::nullopt,
      .shell = false,
  };
  const auto process = run_task(task, project_root);
  if (!process.launched) return {.succeeded = false, .output = {}, .error = process.error};
  const bool succeeded = process.exit_code == 0;
  return {
      .succeeded = succeeded,
      .output = process.standard_output,
      .error = succeeded ? std::string{} : process.standard_error,
  };
}

}  // namespace

GitStatusResult parse_git_porcelain_z(const std::string_view output) {
  GitStatusResult result;
  std::size_t offset = 0;
  while (offset < output.size()) {
    const auto record = next_record(output, offset);
    if (!record || record->size() < 4U || (*record)[2] != ' ') {
      result.error = "Git returned malformed porcelain status";
      return result;
    }
    GitStatusEntry entry{
        .index_status = (*record)[0],
        .worktree_status = (*record)[1],
        .path = std::string(record->substr(3)),
        .original_path = std::nullopt,
    };
    if (is_rename_or_copy(entry.index_status) || is_rename_or_copy(entry.worktree_status)) {
      const auto original = next_record(output, offset);
      if (!original) {
        result.error = "Git returned an incomplete rename status";
        return result;
      }
      entry.original_path = std::string(*original);
    }
    result.entries.push_back(std::move(entry));
  }
  return result;
}

GitStatusResult read_git_status(const std::filesystem::path& project_root) {
  const TaskConfig task{
      .name = "git status",
      .command = {"git", "status", "--porcelain=v1", "-z", "--untracked-files=all"},
      .cwd = std::nullopt,
      .shell = false,
  };
  const auto process = run_task(task, project_root);
  if (!process.launched) return {.entries = {}, .error = process.error};
  if (process.exit_code != 0) {
    return {.entries = {},
            .error = process.standard_error.empty() ? "git status failed" : process.standard_error};
  }
  return parse_git_porcelain_z(process.standard_output);
}

GitRepositoryInfo read_git_repository_info(const std::filesystem::path& project_root) {
  GitRepositoryInfo info;
  const auto status = read_git_status(project_root);
  if (!status.error.empty()) {
    info.error = status.error;
    return info;
  }
  const TaskConfig branch_task{
      .name = "git branch", .command = {"git", "branch", "--show-current"},
      .cwd = std::nullopt, .shell = false};
  const auto branch = run_task(branch_task, project_root);
  if (!branch.launched || branch.exit_code != 0) {
    info.error = branch.error.empty() ? branch.standard_error : branch.error;
    return info;
  }
  info.available = true;
  info.branch = trim_line(branch.standard_output);
  if (info.branch.empty()) info.branch = "detached";
  for (const auto& entry : status.entries) {
    if (entry.index_status == '?') {
      ++info.untracked;
    } else if (entry.index_status != ' ') {
      ++info.staged;
    }
    if (entry.worktree_status != ' ' && entry.worktree_status != '?') ++info.modified;
  }
  return info;
}

std::string format_git_status(const GitStatusResult& status) {
  if (!status.error.empty()) return "Git status error:\n" + status.error;
  if (status.entries.empty()) return "Working tree clean.\n";
  std::string output = "XY  Path\n";
  for (const auto& entry : status.entries) {
    output += entry.index_status;
    output += entry.worktree_status;
    output += "  ";
    output += entry.path.string();
    if (entry.original_path) output += "  <-  " + entry.original_path->string();
    output += '\n';
  }
  return output;
}

GitCommandResult stage_git_file(const std::filesystem::path& project_root,
                                const std::filesystem::path& file) {
  std::string error;
  const auto relative = project_relative_path(project_root, file, error);
  if (!relative) return {.succeeded = false, .output = {}, .error = std::move(error)};
  return run_git_file_command(project_root, {"git", "add", "--", relative->string()});
}

GitCommandResult unstage_git_file(const std::filesystem::path& project_root,
                                  const std::filesystem::path& file) {
  std::string error;
  const auto relative = project_relative_path(project_root, file, error);
  if (!relative) return {.succeeded = false, .output = {}, .error = std::move(error)};
  auto result = run_git_file_command(
      project_root, {"git", "restore", "--staged", "--", relative->string()});
  if (result.succeeded) return result;
  // A repository without HEAD cannot use restore --staged. Removing only the
  // index entry is the safe equivalent for a newly added file.
  return run_git_file_command(
      project_root, {"git", "rm", "--cached", "--quiet", "--", relative->string()});
}

GitCommandResult commit_git(const std::filesystem::path& project_root, const std::string& message) {
  if (message.empty()) {
    return {.succeeded = false, .output = {}, .error = "commit message cannot be empty"};
  }
  return run_git_file_command(project_root, {"git", "commit", "-m", message});
}

GitCommandResult read_git_diff(const std::filesystem::path& project_root,
                               const std::filesystem::path& file, const bool staged) {
  std::string error;
  const auto relative = project_relative_path(project_root, file, error);
  if (!relative) return {.succeeded = false, .output = {}, .error = std::move(error)};
  std::vector<std::string> command{"git", "diff", "--no-ext-diff", "--unified=3"};
  if (staged) command.push_back("--cached");
  command.insert(command.end(), {"--", relative->string()});
  return run_git_file_command(project_root, command);
}

GitCommandResult read_git_history(const std::filesystem::path& project_root,
                                  const std::size_t maximum_entries,
                                  const std::size_t skip_entries) {
  return run_git_file_command(project_root,
                              {"git", "log", "--graph", "--decorate", "--oneline", "-n",
                               std::to_string(maximum_entries), "--skip",
                               std::to_string(skip_entries)});
}

}  // namespace vijai
