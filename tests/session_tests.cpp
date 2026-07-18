#include "horcrux/session.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

namespace {

void session_tests() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("horcrux-session-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::string error;
  assert(!horcrux::load_document_session(root, "/project/main.cpp", error));
  assert(error.empty());
  const horcrux::DocumentSession expected{.cursor_byte = 42U, .top_line = 7U};
  assert(horcrux::save_document_session(root, "/project/main.cpp", expected, error));
  const auto restored = horcrux::load_document_session(root, "/project/main.cpp", error);
  assert(restored);
  assert(restored->cursor_byte == 42U);
  assert(restored->top_line == 7U);
  std::filesystem::remove_all(root);
}

struct RunSessionTests {
  RunSessionTests() { session_tests(); }
};

RunSessionTests run_session_tests;

}  // namespace
