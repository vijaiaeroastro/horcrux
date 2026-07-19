#include "vijai/cpp_tests.hpp"

#include <cassert>

namespace {

void cpp_tests_tests() {
  constexpr auto listing = R"({"backtraceGraph":{"files":["CMakeLists.txt"],"nodes":[{}, {"file":0,"line":42}]},"tests":[{"name":"unit smoke","backtrace":1},{"name":"editor"}]})";
  const auto tests = vijai::parse_ctest_test_listing(listing);
  assert(tests.size() == 2U);
  assert(tests[0].name == "unit smoke");
  assert(tests[0].definition_file == "CMakeLists.txt");
  assert(tests[0].definition_line == 42U);
  assert(tests[0].state == vijai::CppTestState::not_run);
  assert(vijai::parse_ctest_test_listing("not json").empty());
}

struct RunCppTestsTests {
  RunCppTestsTests() { cpp_tests_tests(); }
};

RunCppTestsTests run_cpp_tests_tests;

}  // namespace
