#include "vijai/file_tree.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void file_tree_tests() {
  assert(vijai::detect_license_kind("MIT License\n") == "MIT");
  assert(vijai::detect_license_kind("SPDX-License-Identifier: Apache-2.0\n") == "Apache-2.0");
  assert(vijai::detect_license_kind("GNU GENERAL PUBLIC LICENSE\nVersion 3\n") == "GPL-3.0");
  const auto root = std::filesystem::temp_directory_path() /
                    ("vijai-tree-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root / "src");
  std::filesystem::create_directories(root / "include");
  std::filesystem::create_directories(root / "build");
  std::filesystem::create_directories(root / ".idea");
  std::ofstream(root / "README.md") << "read me\n";
  std::ofstream(root / ".env") << "hidden\n";
  std::ofstream(root / "src" / "main.cpp") << "int main() {}\n";
  std::ofstream(root / "build" / "generated.cpp") << "ignored\n";

  vijai::FileTree tree(root);
  std::string error;
  assert(tree.refresh(error));
  assert(tree.entries().size() == 3U);
  assert(tree.entries()[0].directory);
  assert(tree.entries()[0].relative_path == "include");
  assert(!tree.showing_hidden_files());
  assert(tree.toggle_hidden_files(error));
  assert(tree.showing_hidden_files());
  assert(tree.entries().size() == 5U);
  assert(tree.entries()[0].relative_path == ".idea");
  assert(tree.entries()[1].relative_path == "include");
  assert(tree.toggle_hidden_files(error));
  assert(!tree.showing_hidden_files());
  tree.select_next();
  assert(tree.selected_entry()->relative_path == "src");
  assert(tree.toggle_selected(error));
  assert(tree.entries().size() == 4U);
  assert(tree.entries()[2].relative_path == std::filesystem::path("src") / "main.cpp");
  assert(tree.entries()[2].depth == 1U);
  tree.select(99U);
  assert(tree.selected_entry()->relative_path == "README.md");
  tree.select(0U);
  assert(tree.selected_entry()->relative_path == "include");
  std::filesystem::remove_all(root);
}

struct RunFileTreeTests {
  RunFileTreeTests() { file_tree_tests(); }
};

RunFileTreeTests run_file_tree_tests;

}  // namespace
