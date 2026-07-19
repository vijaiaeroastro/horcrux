#include "vijai/tooling.hpp"

#include <cassert>

namespace {

void tooling_tests() {
  const auto missing = vijai::find_executable("vijai-tool-that-does-not-exist-6e3b0e");
  assert(missing.empty());
  const auto tools = vijai::detect_developer_tools();
  assert(!tools.empty());
  assert(tools.front().name == "git");
}

struct RunToolingTests {
  RunToolingTests() { tooling_tests(); }
};

RunToolingTests run_tooling_tests;

}  // namespace
