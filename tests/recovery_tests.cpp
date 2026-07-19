#include "vijai/recovery.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void recovery_tests() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("vijai-recovery-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::string identity = "/project/example.cpp";
  vijai::RecoveryJournal journal(root, identity);
  std::string error;
  assert(!journal.latest_snapshot(error));
  assert(error.empty());
  assert(journal.append_snapshot("first", error));
  assert(journal.append_snapshot("second", error));
  assert(journal.latest_snapshot(error) == "second");

  const auto complete_size = std::filesystem::file_size(journal.path());
  assert(journal.append_snapshot("incomplete", error));
  std::filesystem::resize_file(journal.path(), std::filesystem::file_size(journal.path()) - 3U);
  assert(journal.latest_snapshot(error) == "second");
  std::filesystem::resize_file(journal.path(), complete_size);

  assert(journal.clear(error));
  assert(!std::filesystem::exists(journal.path()));
  std::filesystem::remove_all(root);
}

struct RunRecoveryTests {
  RunRecoveryTests() { recovery_tests(); }
};

RunRecoveryTests run_recovery_tests;

}  // namespace
