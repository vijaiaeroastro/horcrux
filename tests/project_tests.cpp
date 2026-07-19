#include "vijai/project.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

namespace {

void project_tests() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("vijai-project-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto nested = root / "src" / "nested";
  std::filesystem::create_directories(nested);
  std::filesystem::create_directory(root / ".git");
  const auto discovered = vijai::discover_project_root(nested / "main.cpp");
  assert(discovered);
  assert(*discovered == std::filesystem::weakly_canonical(root));
  const auto previous_directory = std::filesystem::current_path();
  std::filesystem::current_path(root);
  const auto relative_discovered = vijai::discover_project_root("new-file.cpp");
  std::filesystem::current_path(previous_directory);
  assert(relative_discovered);
  assert(*relative_discovered == std::filesystem::weakly_canonical(root));

  const auto state = root / "state";
  std::string error;
  assert(!vijai::is_project_trusted(state, root));
  assert(vijai::set_project_trusted(state, root, true, error));
  assert(vijai::is_project_trusted(state, root));
  assert(vijai::set_project_trusted(state, root, false, error));
  assert(!vijai::is_project_trusted(state, root));
  std::filesystem::remove_all(root);
}

struct RunProjectTests {
  RunProjectTests() { project_tests(); }
};

RunProjectTests run_project_tests;

}  // namespace
