#include "vijai/git.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "vijai/config.hpp"
#include "vijai/task_runner.hpp"

namespace {

void git_tests() {
  std::string porcelain;
  porcelain.append(" M file.cpp", 11U);
  porcelain.push_back('\0');
  porcelain.append("R  renamed.cpp", 14U);
  porcelain.push_back('\0');
  porcelain.append("old.cpp", 7U);
  porcelain.push_back('\0');
  porcelain.append("?? new.txt", 10U);
  porcelain.push_back('\0');

  const auto parsed = vijai::parse_git_porcelain_z(porcelain);
  assert(parsed.error.empty());
  assert(parsed.entries.size() == 3U);
  assert(parsed.entries[0].worktree_status == 'M');
  assert(parsed.entries[0].path == "file.cpp");
  assert(parsed.entries[1].index_status == 'R');
  assert(parsed.entries[1].path == "renamed.cpp");
  assert(parsed.entries[1].original_path == "old.cpp");
  assert(parsed.entries[2].index_status == '?');
  assert(vijai::format_git_status(parsed).find("??  new.txt") != std::string::npos);

  const auto malformed = vijai::parse_git_porcelain_z("M file.cpp");
  assert(!malformed.error.empty());

  const auto root = std::filesystem::temp_directory_path() /
                    ("vijai-git-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(root);
  const vijai::TaskConfig initialize{
      .name = "init", .command = {"git", "init", "--quiet"}, .cwd = std::nullopt, .shell = false};
  assert(vijai::run_task(initialize, root).exit_code == 0);
  const auto file = root / "new file.txt";
  {
    std::ofstream output(file);
    output << "content\n";
  }
  assert(vijai::stage_git_file(root, file).succeeded);
  auto status = vijai::read_git_status(root);
  assert(status.error.empty());
  assert(status.entries.size() == 1U);
  assert(status.entries.front().index_status == 'A');
  assert(vijai::unstage_git_file(root, file).succeeded);
  status = vijai::read_git_status(root);
  assert(status.entries.front().index_status == '?');
  assert(vijai::stage_git_file(root, file).succeeded);
  const vijai::TaskConfig identity_name{
      .name = "name",
      .command = {"git", "config", "user.name", "Vijai Test"},
      .cwd = std::nullopt,
      .shell = false};
  const vijai::TaskConfig identity_email{
      .name = "email",
      .command = {"git", "config", "user.email", "vijai-test@example.invalid"},
      .cwd = std::nullopt,
      .shell = false};
  const vijai::TaskConfig disable_signing{
      .name = "disable signing",
      .command = {"git", "config", "commit.gpgsign", "false"},
      .cwd = std::nullopt,
      .shell = false};
  assert(vijai::run_task(identity_name, root).exit_code == 0);
  assert(vijai::run_task(identity_email, root).exit_code == 0);
  assert(vijai::run_task(disable_signing, root).exit_code == 0);
  const auto committed = vijai::commit_git(root, "initial test commit");
  assert(committed.succeeded);
  assert(vijai::read_git_status(root).entries.empty());
  {
    std::ofstream output(file, std::ios::app);
    output << "changed\n";
  }
  const auto working_diff = vijai::read_git_diff(root, file, false);
  assert(working_diff.succeeded);
  assert(working_diff.output.find("+changed") != std::string::npos);
  assert(vijai::stage_git_file(root, file).succeeded);
  const auto staged_diff = vijai::read_git_diff(root, file, true);
  assert(staged_diff.succeeded);
  assert(staged_diff.output.find("+changed") != std::string::npos);
  const auto history = vijai::read_git_history(root);
  assert(history.succeeded);
  assert(history.output.find("initial test commit") != std::string::npos);
  std::filesystem::remove_all(root);
}

struct RunGitTests {
  RunGitTests() { git_tests(); }
};

RunGitTests run_git_tests;

}  // namespace
