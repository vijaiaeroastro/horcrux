#include "horcrux/project_search.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void project_search_tests() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("horcrux-search-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root / "nested");
  {
    std::ofstream(root / "nested" / "sample.cpp") << "int main() { return needle; }\nneedle\n";
    std::ofstream(root / "binary.dat", std::ios::binary) << "a\0needle";
  }
  const horcrux::ProjectSearch search(root);
  const auto result = search.find_literal("needle");
  assert(result.error.empty());
  assert(result.matches.size() == 2U);
  assert(result.matches.front().path == std::filesystem::path("nested/sample.cpp"));
  assert(result.matches.front().line == 1U);
  assert(result.matches.front().column == 21U);
  assert(!search.find_literal("").error.empty());
  std::filesystem::remove_all(root);
}

struct RunProjectSearchTests {
  RunProjectSearchTests() { project_search_tests(); }
};

RunProjectSearchTests run_project_search_tests;

}  // namespace
