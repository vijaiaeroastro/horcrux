#include "horcrux/git.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "horcrux/config.hpp"
#include "horcrux/task_runner.hpp"

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

  const auto parsed = horcrux::parse_git_porcelain_z(porcelain);
  assert(parsed.error.empty());
  assert(parsed.entries.size() == 3U);
  assert(parsed.entries[0].worktree_status == 'M');
  assert(parsed.entries[0].path == "file.cpp");
  assert(parsed.entries[1].index_status == 'R');
  assert(parsed.entries[1].path == "renamed.cpp");
  assert(parsed.entries[1].original_path == "old.cpp");
  assert(parsed.entries[2].index_status == '?');
  assert(horcrux::format_git_status(parsed).find("??  new.txt") != std::string::npos);

  const auto malformed = horcrux::parse_git_porcelain_z("M file.cpp");
  assert(!malformed.error.empty());

  const auto root = std::filesystem::temp_directory_path() /
                    ("horcrux-git-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(root);
  const horcrux::TaskConfig initialize{
      .name = "init", .command = {"git", "init", "--quiet"}, .cwd = std::nullopt, .shell = false};
  assert(horcrux::run_task(initialize, root).exit_code == 0);
  const auto file = root / "new file.txt";
  {
    std::ofstream output(file);
    output << "content\n";
  }
  assert(horcrux::stage_git_file(root, file).succeeded);
  auto status = horcrux::read_git_status(root);
  assert(status.error.empty());
  assert(status.entries.size() == 1U);
  assert(status.entries.front().index_status == 'A');
  assert(horcrux::unstage_git_file(root, file).succeeded);
  status = horcrux::read_git_status(root);
  assert(status.entries.front().index_status == '?');
  assert(horcrux::stage_git_file(root, file).succeeded);
  const horcrux::TaskConfig identity_name{
      .name = "name",
      .command = {"git", "config", "user.name", "Horcrux Test"},
      .cwd = std::nullopt,
      .shell = false};
  const horcrux::TaskConfig identity_email{
      .name = "email",
      .command = {"git", "config", "user.email", "horcrux-test@example.invalid"},
      .cwd = std::nullopt,
      .shell = false};
  const horcrux::TaskConfig disable_signing{
      .name = "disable signing",
      .command = {"git", "config", "commit.gpgsign", "false"},
      .cwd = std::nullopt,
      .shell = false};
  assert(horcrux::run_task(identity_name, root).exit_code == 0);
  assert(horcrux::run_task(identity_email, root).exit_code == 0);
  assert(horcrux::run_task(disable_signing, root).exit_code == 0);
  const auto committed = horcrux::commit_git(root, "initial test commit");
  assert(committed.succeeded);
  assert(horcrux::read_git_status(root).entries.empty());
  std::filesystem::remove_all(root);
}

struct RunGitTests {
  RunGitTests() { git_tests(); }
};

RunGitTests run_git_tests;

}  // namespace
