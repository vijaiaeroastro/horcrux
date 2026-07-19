#include "vijai/syntax_highlighter.hpp"

#include <tree_sitter/api.h>
#include <tree-sitter-cpp.h>
#include <tree-sitter-typst.h>
#include <tree_sitter/tree-sitter-bibtex.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

namespace vijai {
namespace {

constexpr std::string_view cpp_highlight_query = R"(
(comment) @comment
[(string_literal) (raw_string_literal) (char_literal)] @string
(number_literal) @number
[(primitive_type) (type_identifier) (namespace_identifier)] @type
[(preproc_include) (preproc_def) (preproc_function_def) (preproc_if) (preproc_ifdef) (preproc_call)] @preprocessor
[
 "catch" "class" "co_await" "co_return" "co_yield" "constexpr" "constinit"
 "consteval" "delete" "explicit" "final" "friend" "mutable" "namespace"
 "noexcept" "new" "override" "private" "protected" "public" "template" "throw"
 "try" "typename" "using" "concept" "requires" "virtual" "import" "export" "module"
] @keyword
(auto) @type
)";

constexpr std::string_view typst_highlight_query = R"(
(comment) @comment
(string) @string
(number) @number
[(let) (branch) (while) (for) (import) (include) (show) (set) (return) (flow)] @keyword
)";

constexpr std::string_view bibtex_highlight_query = R"(
[(string_type) (preamble_type) (entry_type)] @keyword
[(junk) (comment)] @comment
(number) @number
[(brace_word) (quote_word)] @string
)";

bool is_word_character(const unsigned char value) {
  return std::isalnum(value) != 0 || value == '_';
}

bool contains(const std::string_view word, const std::initializer_list<std::string_view> words) {
  return std::find(words.begin(), words.end(), word) != words.end();
}

int priority(const SyntaxKind kind) {
  switch (kind) {
    case SyntaxKind::preprocessor: return 5;
    case SyntaxKind::comment: return 4;
    case SyntaxKind::string: return 3;
    case SyntaxKind::keyword: return 2;
    case SyntaxKind::type: return 2;
    case SyntaxKind::number: return 1;
    case SyntaxKind::plain: return 0;
  }
  return 0;
}

std::optional<SyntaxKind> syntax_kind_for_capture(const char* name) {
  const std::string_view capture(name);
  if (capture == "comment") return SyntaxKind::comment;
  if (capture == "string") return SyntaxKind::string;
  if (capture == "number") return SyntaxKind::number;
  if (capture == "keyword") return SyntaxKind::keyword;
  if (capture == "type") return SyntaxKind::type;
  if (capture == "preprocessor") return SyntaxKind::preprocessor;
  return std::nullopt;
}

const TSLanguage* tree_sitter_language_for(const SyntaxHighlighter::Language language) {
  switch (language) {
    case SyntaxHighlighter::Language::cpp: return tree_sitter_cpp();
    case SyntaxHighlighter::Language::typst: return tree_sitter_typst();
    case SyntaxHighlighter::Language::bibtex: return tree_sitter_bibtex();
    default: return nullptr;
  }
}

std::string_view tree_sitter_query_for(const SyntaxHighlighter::Language language) {
  switch (language) {
    case SyntaxHighlighter::Language::cpp: return cpp_highlight_query;
    case SyntaxHighlighter::Language::typst: return typst_highlight_query;
    case SyntaxHighlighter::Language::bibtex: return bibtex_highlight_query;
    default: return {};
  }
}

}  // namespace

struct SyntaxHighlighter::Impl {
  Impl() {
    parser = ts_parser_new();
  }

  ~Impl() {
    if (query != nullptr) ts_query_delete(query);
    if (tree != nullptr) ts_tree_delete(tree);
    if (parser != nullptr) ts_parser_delete(parser);
  }

  TSParser* parser{nullptr};
  TSTree* tree{nullptr};
  TSQuery* query{nullptr};
  const TSLanguage* language{nullptr};
  std::string source;
  std::vector<SyntaxSpan> spans;
};

SyntaxHighlighter::SyntaxHighlighter(const std::filesystem::path path)
    : impl_(std::make_unique<Impl>()) {
  set_path(path);
}

SyntaxHighlighter::~SyntaxHighlighter() = default;
SyntaxHighlighter::SyntaxHighlighter(SyntaxHighlighter&&) noexcept = default;
SyntaxHighlighter& SyntaxHighlighter::operator=(SyntaxHighlighter&&) noexcept = default;

void SyntaxHighlighter::set_path(const std::filesystem::path& path) {
  const auto previous_language = language_;
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  if (contains(extension, {".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"})) {
    language_ = Language::cpp;
  } else if (extension == ".c") {
    language_ = Language::c_family;
  } else if (extension == ".py") {
    language_ = Language::python;
  } else if (extension == ".go") {
    language_ = Language::go;
  } else if (contains(extension, {".js", ".jsx", ".ts", ".tsx", ".mjs", ".cjs"})) {
    language_ = Language::javascript;
  } else if (extension == ".json") {
    language_ = Language::json;
  } else if (contains(extension, {".md", ".markdown"})) {
    language_ = Language::markdown;
  } else if (contains(extension, {".yaml", ".yml"})) {
    language_ = Language::yaml;
  } else if (extension == ".toml") {
    language_ = Language::toml;
  } else if (extension == ".typ") {
    language_ = Language::typst;
  } else if (contains(extension, {".tex", ".latex", ".sty", ".cls"})) {
    language_ = Language::tex;
  } else if (extension == ".bib") {
    language_ = Language::bibtex;
  } else {
    language_ = Language::plain;
  }
  if (impl_ && previous_language != language_) {
    impl_->source.clear();
    impl_->spans.clear();
  }
}

void SyntaxHighlighter::set_source(const std::string_view source) {
  const auto* grammar = tree_sitter_language_for(language_);
  const auto query_source = tree_sitter_query_for(language_);
  if (grammar == nullptr || query_source.empty() || impl_ == nullptr || impl_->source == source) return;
  if (impl_->language != grammar) {
    if (impl_->query != nullptr) {
      ts_query_delete(impl_->query);
      impl_->query = nullptr;
    }
    if (impl_->tree != nullptr) {
      ts_tree_delete(impl_->tree);
      impl_->tree = nullptr;
    }
    if (impl_->parser != nullptr) ts_parser_set_language(impl_->parser, grammar);
    std::uint32_t error_offset = 0U;
    TSQueryError error = TSQueryErrorNone;
    impl_->query = ts_query_new(grammar, query_source.data(),
                                static_cast<std::uint32_t>(query_source.size()),
                                &error_offset, &error);
    impl_->language = grammar;
  }
  impl_->source.assign(source);
  if (impl_->tree != nullptr) {
    ts_tree_delete(impl_->tree);
    impl_->tree = nullptr;
  }
  impl_->spans.clear();
  if (impl_->parser == nullptr || impl_->query == nullptr) return;
  impl_->tree = ts_parser_parse_string(impl_->parser, nullptr, impl_->source.data(),
                                       static_cast<std::uint32_t>(impl_->source.size()));
  if (impl_->tree == nullptr) return;

  std::vector<SyntaxKind> kinds(impl_->source.size(), SyntaxKind::plain);
  TSQueryCursor* cursor = ts_query_cursor_new();
  ts_query_cursor_exec(cursor, impl_->query, ts_tree_root_node(impl_->tree));
  TSQueryMatch match;
  std::uint32_t capture_index = 0U;
  while (ts_query_cursor_next_capture(cursor, &match, &capture_index)) {
    const auto& capture = match.captures[capture_index];
    std::uint32_t name_length = 0U;
    const char* name = ts_query_capture_name_for_id(impl_->query, capture.index, &name_length);
    if (name == nullptr) continue;
    const auto kind = syntax_kind_for_capture(std::string(name, name_length).c_str());
    if (!kind) continue;
    const std::size_t start = ts_node_start_byte(capture.node);
    const std::size_t end = std::min<std::size_t>(ts_node_end_byte(capture.node), kinds.size());
    for (std::size_t index = start; index < end; ++index) {
      if (priority(*kind) >= priority(kinds[index])) kinds[index] = *kind;
    }
  }
  ts_query_cursor_delete(cursor);

  for (std::size_t start = 0U; start < kinds.size();) {
    const auto kind = kinds[start];
    std::size_t end = start + 1U;
    while (end < kinds.size() && kinds[end] == kind) ++end;
    if (kind != SyntaxKind::plain) impl_->spans.push_back({start, end - start, kind});
    start = end;
  }
}

std::vector<SyntaxSpan> SyntaxHighlighter::highlight_line(const std::string_view line,
                                                           const std::size_t source_offset) const {
  if (tree_sitter_language_for(language_) != nullptr && impl_ && !impl_->spans.empty()) {
    std::vector<SyntaxSpan> spans;
    const auto line_end = source_offset + line.size();
    for (const auto& span : impl_->spans) {
      const auto span_end = span.start + span.length;
      if (span_end <= source_offset || span.start >= line_end) continue;
      const auto start = std::max(span.start, source_offset);
      const auto end = std::min(span_end, line_end);
      spans.push_back({start - source_offset, end - start, span.kind});
    }
    return spans;
  }

  std::vector<SyntaxSpan> spans;
  std::size_t index = 0U;
  while (index < line.size()) {
    const auto current = static_cast<unsigned char>(line[index]);
    if ((language_ == Language::tex || language_ == Language::bibtex) && line[index] == '%') {
      spans.push_back({index, line.size() - index, SyntaxKind::comment});
      break;
    }
    if (language_ == Language::typst && index + 1U < line.size() && line[index] == '/' &&
        line[index + 1U] == '/') {
      spans.push_back({index, line.size() - index, SyntaxKind::comment});
      break;
    }
    if (language_ == Language::typst && line[index] == '#') {
      const auto start = index++;
      while (index < line.size() && is_word_character(static_cast<unsigned char>(line[index]))) ++index;
      spans.push_back({start, index - start, SyntaxKind::preprocessor});
      continue;
    }
    if (language_ == Language::tex && line[index] == '\\') {
      const auto start = index++;
      while (index < line.size() && std::isalpha(static_cast<unsigned char>(line[index])) != 0) ++index;
      spans.push_back({start, index - start, SyntaxKind::keyword});
      continue;
    }
    if (language_ == Language::bibtex && line[index] == '@') {
      const auto start = index++;
      while (index < line.size() && std::isalpha(static_cast<unsigned char>(line[index])) != 0) ++index;
      spans.push_back({start, index - start, SyntaxKind::keyword});
      continue;
    }
    if (supports_slash_comments() && index + 1U < line.size() && line[index] == '/' &&
        line[index + 1U] == '*') {
      const auto start = index;
      const auto close = line.find("*/", index + 2U);
      index = close == std::string_view::npos ? line.size() : close + 2U;
      spans.push_back({start, index - start, SyntaxKind::comment});
      continue;
    }
    if (supports_slash_comments() && index + 1U < line.size() && line[index] == '/' &&
        line[index + 1U] == '/') {
      spans.push_back({index, line.size() - index, SyntaxKind::comment});
      break;
    }
    if (supports_hash_comments() && line[index] == '#') {
      const bool preprocessor = language_ == Language::c_family;
      spans.push_back({index, line.size() - index,
                       preprocessor ? SyntaxKind::preprocessor : SyntaxKind::comment});
      break;
    }
    if (line[index] == '"' || line[index] == '\'') {
      const char quote = line[index];
      const auto start = index++;
      while (index < line.size()) {
        if (line[index] == '\\' && index + 1U < line.size()) {
          index += 2U;
        } else if (line[index++] == quote) {
          break;
        }
      }
      spans.push_back({start, index - start, SyntaxKind::string});
      continue;
    }
    if (std::isdigit(current) != 0) {
      const auto start = index++;
      while (index < line.size() &&
             (std::isalnum(static_cast<unsigned char>(line[index])) != 0 || line[index] == '.' ||
              line[index] == '_')) {
        ++index;
      }
      spans.push_back({start, index - start, SyntaxKind::number});
      continue;
    }
    if (is_word_character(current)) {
      const auto start = index++;
      while (index < line.size() && is_word_character(static_cast<unsigned char>(line[index]))) ++index;
      const auto word = line.substr(start, index - start);
      if (is_keyword(word)) spans.push_back({start, index - start, SyntaxKind::keyword});
      continue;
    }
    ++index;
  }
  return spans;
}

bool SyntaxHighlighter::is_keyword(const std::string_view word) const {
  switch (language_) {
    case Language::c_family:
      return contains(word, {"alignas", "alignof", "auto", "bool", "break", "case", "catch",
                             "char", "class", "const", "constexpr", "continue", "default", "delete",
                             "do", "double", "else", "enum", "explicit", "export", "extern", "false",
                             "float", "for", "friend", "if", "inline", "int", "long", "namespace",
                             "new", "nullptr", "operator", "private", "protected", "public", "return",
                             "short", "signed", "sizeof", "static", "struct", "switch", "template",
                             "this", "throw", "true", "try", "typename", "union", "unsigned", "using",
                             "virtual", "void", "volatile", "while"});
    case Language::python:
      return contains(word, {"and", "as", "assert", "async", "await", "break", "class", "continue",
                             "def", "del", "elif", "else", "except", "False", "finally", "for", "from",
                             "global", "if", "import", "in", "is", "lambda", "None", "nonlocal", "not",
                             "or", "pass", "raise", "return", "True", "try", "while", "with", "yield"});
    case Language::go:
      return contains(word, {"break", "case", "chan", "const", "continue", "default", "defer", "else",
                             "fallthrough", "for", "func", "go", "goto", "if", "import", "interface", "map",
                             "package", "range", "return", "select", "struct", "switch", "type", "var"});
    case Language::javascript:
      return contains(word, {"as", "async", "await", "break", "case", "catch", "class", "const", "continue",
                             "default", "delete", "do", "else", "export", "extends", "false", "finally", "for",
                             "from", "function", "if", "import", "in", "instanceof", "let", "new", "null",
                             "return", "static", "switch", "this", "throw", "true", "try", "typeof", "undefined",
                             "var", "void", "while", "yield"});
    case Language::json:
      return contains(word, {"true", "false", "null"});
    case Language::typst:
      return contains(word, {"let", "set", "show", "import", "include", "if", "else", "for",
                             "while", "break", "continue", "return", "none", "auto", "true", "false"});
    case Language::bibtex:
      return contains(word, {"author", "title", "year", "journal", "booktitle", "publisher", "volume",
                             "number", "pages", "doi", "url", "editor", "edition", "month", "note"});
    default: return false;
  }
}

bool SyntaxHighlighter::supports_hash_comments() const {
  return language_ == Language::python || language_ == Language::yaml || language_ == Language::toml;
}

bool SyntaxHighlighter::supports_slash_comments() const {
  return language_ == Language::c_family || language_ == Language::go ||
         language_ == Language::javascript;
}

}  // namespace vijai
