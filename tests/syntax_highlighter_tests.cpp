#include "horcrux/syntax_highlighter.hpp"

#include <algorithm>
#include <cassert>

namespace {

void syntax_highlighter_tests() {
  horcrux::SyntaxHighlighter cpp("sample.cpp");
  const std::string source = "constexpr auto name = \"Horcrux\"; // comment\n/* note */ int value";
  cpp.set_source(source);
  const auto cpp_spans = cpp.highlight_line("constexpr auto name = \"Horcrux\"; // comment");
  assert(std::any_of(cpp_spans.begin(), cpp_spans.end(), [](const horcrux::SyntaxSpan& span) {
    return span.kind == horcrux::SyntaxKind::keyword;
  }));
  assert(std::any_of(cpp_spans.begin(), cpp_spans.end(), [](const horcrux::SyntaxSpan& span) {
    return span.kind == horcrux::SyntaxKind::string;
  }));
  assert(std::any_of(cpp_spans.begin(), cpp_spans.end(), [](const horcrux::SyntaxSpan& span) {
    return span.kind == horcrux::SyntaxKind::comment;
  }));
  const auto block_comment = cpp.highlight_line("/* note */ int value", source.find('\n') + 1U);
  assert(std::any_of(block_comment.begin(), block_comment.end(), [](const horcrux::SyntaxSpan& span) {
    return span.kind == horcrux::SyntaxKind::comment;
  }));
  assert(std::any_of(block_comment.begin(), block_comment.end(), [](const horcrux::SyntaxSpan& span) {
    return span.kind == horcrux::SyntaxKind::type;
  }));

  horcrux::SyntaxHighlighter python("sample.py");
  const auto python_spans = python.highlight_line("def value(): # 42");
  assert(python_spans.size() == 2U);
  assert(python_spans[0].kind == horcrux::SyntaxKind::keyword);
  assert(python_spans[1].kind == horcrux::SyntaxKind::comment);

  horcrux::SyntaxHighlighter plain("README.txt");
  assert(plain.highlight_line("not highlighted").empty());
}

struct RunSyntaxHighlighterTests {
  RunSyntaxHighlighterTests() { syntax_highlighter_tests(); }
};

RunSyntaxHighlighterTests run_syntax_highlighter_tests;

}  // namespace
